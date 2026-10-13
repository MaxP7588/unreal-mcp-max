#include "Commands/UnrealMCPInputCommands.h"

#include "Commands/UnrealMCPCommonUtils.h"
#include "Commands/UnrealMCPGraphSerializer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "EditorAssetLibrary.h"
#include "Misc/PackageName.h"

namespace
{
    FString ValueTypeToString(EInputActionValueType Type)
    {
        switch (Type)
        {
        case EInputActionValueType::Boolean: return TEXT("Boolean");
        case EInputActionValueType::Axis1D: return TEXT("Axis1D");
        case EInputActionValueType::Axis2D: return TEXT("Axis2D");
        case EInputActionValueType::Axis3D: return TEXT("Axis3D");
        default: return TEXT("Boolean");
        }
    }

    bool ParseValueType(const FString& Text, EInputActionValueType& OutType)
    {
        if (Text.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase)) { OutType = EInputActionValueType::Axis1D; return true; }
        if (Text.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase)) { OutType = EInputActionValueType::Axis2D; return true; }
        if (Text.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase)) { OutType = EInputActionValueType::Axis3D; return true; }
        if (Text.IsEmpty() || Text.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("Digital"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
        {
            OutType = EInputActionValueType::Boolean;
            return true;
        }
        return false;
    }

    TSharedPtr<FJsonObject> InstancedObjectToJson(const UObject* Object)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        if (!Object)
        {
            return Json;
        }
        Json->SetStringField(TEXT("class"), Object->GetClass()->GetName());
        for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            if (!It->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || It->HasAnyPropertyFlags(CPF_Transient))
            {
                continue;
            }
            FString Exported;
            It->ExportText_InContainer(0, Exported, Object, nullptr, nullptr, PPF_None);
            if (!Exported.IsEmpty())
            {
                Json->SetStringField(It->GetName(), Exported.Left(200));
            }
        }
        return Json;
    }

    TSharedPtr<FJsonObject> ActionToJson(const UInputAction* Action)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("kind"), TEXT("input_action"));
        Json->SetStringField(TEXT("name"), Action->GetName());
        Json->SetStringField(TEXT("path"), Action->GetPathName());
        Json->SetStringField(TEXT("value_type"), ValueTypeToString(Action->ValueType));
        Json->SetBoolField(TEXT("consume_input"), Action->bConsumeInput);
        Json->SetBoolField(TEXT("trigger_when_paused"), Action->bTriggerWhenPaused);
        if (!Action->ActionDescription.IsEmpty())
        {
            Json->SetStringField(TEXT("description"), Action->ActionDescription.ToString());
        }
        TArray<TSharedPtr<FJsonValue>> Triggers;
        for (UInputTrigger* Trigger : Action->Triggers)
        {
            Triggers.Add(MakeShared<FJsonValueObject>(InstancedObjectToJson(Trigger)));
        }
        TArray<TSharedPtr<FJsonValue>> Modifiers;
        for (UInputModifier* Modifier : Action->Modifiers)
        {
            Modifiers.Add(MakeShared<FJsonValueObject>(InstancedObjectToJson(Modifier)));
        }
        Json->SetArrayField(TEXT("triggers"), Triggers);
        Json->SetArrayField(TEXT("modifiers"), Modifiers);
        return Json;
    }

    TSharedPtr<FJsonObject> ContextToJson(const UInputMappingContext* Context)
    {
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("kind"), TEXT("input_mapping_context"));
        Json->SetStringField(TEXT("name"), Context->GetName());
        Json->SetStringField(TEXT("path"), Context->GetPathName());
        if (!Context->ContextDescription.IsEmpty())
        {
            Json->SetStringField(TEXT("description"), Context->ContextDescription.ToString());
        }
        TArray<TSharedPtr<FJsonValue>> Mappings;
        for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("action"), Mapping.Action ? Mapping.Action->GetName() : TEXT(""));
            Item->SetStringField(TEXT("action_path"), Mapping.Action ? Mapping.Action->GetPathName() : TEXT(""));
            Item->SetStringField(TEXT("key"), Mapping.Key.ToString());
            TArray<TSharedPtr<FJsonValue>> Triggers;
            for (const TObjectPtr<UInputTrigger>& Trigger : Mapping.Triggers)
            {
                Triggers.Add(MakeShared<FJsonValueObject>(InstancedObjectToJson(Trigger)));
            }
            TArray<TSharedPtr<FJsonValue>> Modifiers;
            for (const TObjectPtr<UInputModifier>& Modifier : Mapping.Modifiers)
            {
                Modifiers.Add(MakeShared<FJsonValueObject>(InstancedObjectToJson(Modifier)));
            }
            Item->SetArrayField(TEXT("triggers"), Triggers);
            Item->SetArrayField(TEXT("modifiers"), Modifiers);
            Mappings.Add(MakeShared<FJsonValueObject>(Item));
        }
        Json->SetArrayField(TEXT("mappings"), Mappings);
        return Json;
    }

    UObject* LoadInputObject(const FString& Path, FString& OutError)
    {
        const FString ObjectPath = UnrealMCPGraph::NormalizeObjectPath(Path);
        UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath);
        if (!Object)
        {
            OutError = FString::Printf(TEXT("No se encontró el asset: %s"), *ObjectPath);
        }
        return Object;
    }

    UInputAction* FindOrCreateAction(const TSharedPtr<FJsonObject>& Params, FString& OutError, bool& bCreated)
    {
        bCreated = false;
        FString ActionPath;
        if (Params->TryGetStringField(TEXT("action_path"), ActionPath) && !ActionPath.IsEmpty())
        {
            UObject* Object = LoadInputObject(ActionPath, OutError);
            UInputAction* Action = Cast<UInputAction>(Object);
            if (Object && !Action)
            {
                OutError = FString::Printf(TEXT("%s no es un Input Action"), *Object->GetName());
            }
            return Action;
        }

        FString ActionName;
        if (!Params->TryGetStringField(TEXT("action_name"), ActionName) || ActionName.IsEmpty())
        {
            OutError = TEXT("Falta 'action_path' o 'action_name'");
            return nullptr;
        }
        FString Folder = TEXT("/Game/Input/Actions");
        Params->TryGetStringField(TEXT("folder"), Folder);
        Folder.RemoveFromEnd(TEXT("/"));
        const FString PackageName = Folder + TEXT("/") + ActionName;
        if (UEditorAssetLibrary::DoesAssetExist(PackageName))
        {
            return Cast<UInputAction>(LoadInputObject(PackageName, OutError));
        }

        FString ValueTypeText = TEXT("Boolean");
        Params->TryGetStringField(TEXT("value_type"), ValueTypeText);
        EInputActionValueType ValueType;
        if (!ParseValueType(ValueTypeText, ValueType))
        {
            OutError = FString::Printf(TEXT("value_type desconocido: %s"), *ValueTypeText);
            return nullptr;
        }

        UPackage* Package = CreatePackage(*PackageName);
        UInputAction* Action = NewObject<UInputAction>(Package, *ActionName, RF_Public | RF_Standalone | RF_Transactional);
        Action->ValueType = ValueType;
        FAssetRegistryModule::AssetCreated(Action);
        Package->MarkPackageDirty();
        bCreated = true;
        return Action;
    }

    void AddInstanced(UObject* Outer, const TArray<TSharedPtr<FJsonValue>>* Specs, TArray<TObjectPtr<UInputTrigger>>& Triggers, TArray<TObjectPtr<UInputModifier>>& Modifiers, bool bTriggers, TArray<FString>& Errors)
    {
        if (!Specs)
        {
            return;
        }
        for (const TSharedPtr<FJsonValue>& Value : *Specs)
        {
            FString ClassName = Value->Type == EJson::String ? Value->AsString() : FString();
            const TSharedPtr<FJsonObject>* ObjectSpec = nullptr;
            if (Value->TryGetObject(ObjectSpec))
            {
                (*ObjectSpec)->TryGetStringField(TEXT("class"), ClassName);
            }
            FString ResolveError;
            UClass* Class = FUnrealMCPCommonUtils::ResolveClass(ClassName, ResolveError);
            if (!Class)
            {
                Errors.Add(ResolveError);
                continue;
            }
            if (bTriggers && Class->IsChildOf(UInputTrigger::StaticClass()))
            {
                UInputTrigger* Trigger = NewObject<UInputTrigger>(Outer, Class, NAME_None, RF_Transactional);
                Triggers.Add(Trigger);
            }
            else if (!bTriggers && Class->IsChildOf(UInputModifier::StaticClass()))
            {
                UInputModifier* Modifier = NewObject<UInputModifier>(Outer, Class, NAME_None, RF_Transactional);
                Modifiers.Add(Modifier);
            }
            else
            {
                Errors.Add(FString::Printf(TEXT("%s no es un %s"), *ClassName, bTriggers ? TEXT("trigger") : TEXT("modifier")));
            }
        }
    }
}

TSharedPtr<FJsonObject> FUnrealMCPInputCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("read_input_asset")) return HandleReadInputAsset(Params);
    if (CommandType == TEXT("find_input_bindings")) return HandleFindInputBindings(Params);
    if (CommandType == TEXT("add_enhanced_input_mapping")) return HandleAddEnhancedInputMapping(Params);
    if (CommandType == TEXT("remove_enhanced_input_mapping")) return HandleRemoveEnhancedInputMapping(Params);
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown input command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPInputCommands::HandleReadInputAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'asset_path'"));
    }
    FString Error;
    UObject* Object = LoadInputObject(AssetPath, Error);
    if (!Object)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    if (const UInputAction* Action = Cast<UInputAction>(Object))
    {
        return FUnrealMCPCommonUtils::CreateSuccessResponse(ActionToJson(Action));
    }
    if (const UInputMappingContext* Context = Cast<UInputMappingContext>(Object))
    {
        return FUnrealMCPCommonUtils::CreateSuccessResponse(ContextToJson(Context));
    }
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("%s no es Input Action ni Mapping Context"), *Object->GetName()));
}

TSharedPtr<FJsonObject> FUnrealMCPInputCommands::HandleFindInputBindings(const TSharedPtr<FJsonObject>& Params)
{
    FString Query;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("query"), Query) || Query.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'query'"));
    }
    Query.TrimStartAndEndInline();
    FString Path = TEXT("/Game");
    Params->TryGetStringField(TEXT("path"), Path);

    FARFilter Filter;
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;
    Filter.PackagePaths.Add(FName(*Path));
    Filter.ClassPaths.Add(UInputMappingContext::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UInputAction::StaticClass()->GetClassPathName());

    FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Assets;
    AssetRegistry.Get().GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> Hits;
    for (const FAssetData& Asset : Assets)
    {
        if (Asset.IsRedirector() || Hits.Num() >= 80)
        {
            continue;
        }
        UObject* Object = Asset.GetAsset();
        if (UInputAction* Action = Cast<UInputAction>(Object))
        {
            if (Action->GetName().Contains(Query, ESearchCase::IgnoreCase))
            {
                TSharedPtr<FJsonObject> Hit = MakeShared<FJsonObject>();
                Hit->SetStringField(TEXT("kind"), TEXT("action"));
                Hit->SetStringField(TEXT("name"), Action->GetName());
                Hit->SetStringField(TEXT("path"), Action->GetPathName());
                Hit->SetStringField(TEXT("value_type"), ValueTypeToString(Action->ValueType));
                Hits.Add(MakeShared<FJsonValueObject>(Hit));
            }
            continue;
        }
        UInputMappingContext* Context = Cast<UInputMappingContext>(Object);
        if (!Context)
        {
            continue;
        }
        for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
        {
            const FString ActionName = Mapping.Action ? Mapping.Action->GetName() : FString();
            const FString KeyName = Mapping.Key.ToString();
            if (!ActionName.Contains(Query, ESearchCase::IgnoreCase) && !KeyName.Contains(Query, ESearchCase::IgnoreCase))
            {
                continue;
            }
            TSharedPtr<FJsonObject> Hit = MakeShared<FJsonObject>();
            Hit->SetStringField(TEXT("kind"), TEXT("mapping"));
            Hit->SetStringField(TEXT("context"), Context->GetName());
            Hit->SetStringField(TEXT("context_path"), Context->GetPathName());
            Hit->SetStringField(TEXT("action"), ActionName);
            Hit->SetStringField(TEXT("key"), KeyName);
            Hits.Add(MakeShared<FJsonValueObject>(Hit));
            if (Hits.Num() >= 80)
            {
                break;
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("query"), Query);
    Data->SetNumberField(TEXT("count"), Hits.Num());
    Data->SetArrayField(TEXT("hits"), Hits);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPInputCommands::HandleAddEnhancedInputMapping(const TSharedPtr<FJsonObject>& Params)
{
    FString ContextPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("context_path"), ContextPath) || ContextPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'context_path'"));
    }
    FString Error;
    UInputMappingContext* Context = Cast<UInputMappingContext>(LoadInputObject(ContextPath, Error));
    if (!Context)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error.IsEmpty() ? TEXT("El asset no es un Mapping Context") : Error);
    }

    const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
    if (!Params->TryGetArrayField(TEXT("keys"), Keys) || !Keys || Keys->Num() == 0)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'keys'"));
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP add enhanced input mapping"));
    Transaction.Modify(Context);
    bool bCreated = false;
    UInputAction* Action = FindOrCreateAction(Params, Error, bCreated);
    if (!Action)
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    if (bCreated)
    {
        Transaction.Modify(Action);
    }

    TArray<FString> Warnings;
    TArray<TSharedPtr<FJsonValue>> Added;
    for (const TSharedPtr<FJsonValue>& KeyValue : *Keys)
    {
        const FString KeyName = KeyValue->AsString();
        const FKey Key(*KeyName);
        if (!Key.IsValid())
        {
            Warnings.Add(FString::Printf(TEXT("Tecla inválida: %s"), *KeyName));
            continue;
        }
        FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
        const TArray<TSharedPtr<FJsonValue>>* TriggerSpecs = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* ModifierSpecs = nullptr;
        Params->TryGetArrayField(TEXT("triggers"), TriggerSpecs);
        Params->TryGetArrayField(TEXT("modifiers"), ModifierSpecs);
        TArray<TObjectPtr<UInputModifier>> IgnoredModifiers;
        TArray<TObjectPtr<UInputTrigger>> IgnoredTriggers;
        AddInstanced(Context, TriggerSpecs, Mapping.Triggers, IgnoredModifiers, true, Warnings);
        AddInstanced(Context, ModifierSpecs, IgnoredTriggers, Mapping.Modifiers, false, Warnings);
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("action"), Action->GetName());
        Item->SetStringField(TEXT("key"), Key.ToString());
        Added.Add(MakeShared<FJsonValueObject>(Item));
    }
    Context->GetOutermost()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("context"), Context->GetPathName());
    Data->SetStringField(TEXT("action"), Action->GetPathName());
    Data->SetBoolField(TEXT("action_created"), bCreated);
    Data->SetArrayField(TEXT("added"), Added);
    TArray<TSharedPtr<FJsonValue>> WarningValues;
    for (const FString& Warning : Warnings)
    {
        WarningValues.Add(MakeShared<FJsonValueString>(Warning));
    }
    Data->SetArrayField(TEXT("warnings"), WarningValues);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPInputCommands::HandleRemoveEnhancedInputMapping(const TSharedPtr<FJsonObject>& Params)
{
    FString ContextPath;
    FString ActionPath;
    FString KeyName;
    if (!Params.IsValid()
        || !Params->TryGetStringField(TEXT("context_path"), ContextPath)
        || !Params->TryGetStringField(TEXT("action"), ActionPath)
        || !Params->TryGetStringField(TEXT("key"), KeyName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Faltan 'context_path', 'action' y 'key'"));
    }
    FString Error;
    UInputMappingContext* Context = Cast<UInputMappingContext>(LoadInputObject(ContextPath, Error));
    UInputAction* Action = Cast<UInputAction>(LoadInputObject(ActionPath, Error));
    if (!Context || !Action)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se encontró el contexto o la acción"));
    }
    const FKey Key(*KeyName);
    if (!Key.IsValid())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Tecla inválida: %s"), *KeyName));
    }
    FUnrealMCPEditTransaction Transaction(TEXT("MCP remove enhanced input mapping"));
    Transaction.Modify(Context);
    Context->UnmapKey(Action, Key);
    Context->GetOutermost()->MarkPackageDirty();
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("context"), Context->GetPathName());
    Data->SetStringField(TEXT("action"), Action->GetName());
    Data->SetStringField(TEXT("key"), Key.ToString());
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}
