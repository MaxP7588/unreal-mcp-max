#include "Commands/UnrealMCPMaterialCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Engine/Texture.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"

namespace
{
    TSharedPtr<FJsonValue> ColorToJson(const FLinearColor& Color)
    {
        TArray<TSharedPtr<FJsonValue>> Values = {
            MakeShared<FJsonValueNumber>(Color.R), MakeShared<FJsonValueNumber>(Color.G),
            MakeShared<FJsonValueNumber>(Color.B), MakeShared<FJsonValueNumber>(Color.A)
        };
        return MakeShared<FJsonValueArray>(Values);
    }

    bool JsonToColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Value->TryGetArray(Array) && Array->Num() >= 3)
        {
            Out = FLinearColor((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber(), (*Array)[2]->AsNumber(),
                Array->Num() > 3 ? (*Array)[3]->AsNumber() : 1.0);
            return true;
        }
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (Value->TryGetObject(Object))
        {
            double R = 0, G = 0, B = 0, A = 1;
            (*Object)->TryGetNumberField(TEXT("R"), R);
            (*Object)->TryGetNumberField(TEXT("G"), G);
            (*Object)->TryGetNumberField(TEXT("B"), B);
            (*Object)->TryGetNumberField(TEXT("A"), A);
            Out = FLinearColor(R, G, B, A);
            return true;
        }
        FString Hex;
        if (Value->TryGetString(Hex) && Hex.StartsWith(TEXT("#")))
        {
            Out = FLinearColor(FColor::FromHex(Hex));
            return true;
        }
        return false;
    }

    bool HasParameter(UMaterialInterface* Material, const FString& Type, FName Name)
    {
        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Ids;
        if (Type == TEXT("scalar")) Material->GetAllScalarParameterInfo(Infos, Ids);
        else if (Type == TEXT("vector")) Material->GetAllVectorParameterInfo(Infos, Ids);
        else if (Type == TEXT("texture")) Material->GetAllTextureParameterInfo(Infos, Ids);
        else if (Type == TEXT("switch")) Material->GetAllStaticSwitchParameterInfo(Infos, Ids);
        return Infos.ContainsByPredicate([Name](const FMaterialParameterInfo& Info) { return Info.Name == Name; });
    }

    /** Asigna un parámetro; el tipo se deduce del valor si no se indica. */
    bool SetParameter(UMaterialInstanceConstant* Instance, const FString& Name, const TSharedPtr<FJsonValue>& Value, FString Type, FString& OutError)
    {
        const FName ParamName(*Name);
        if (Type.IsEmpty())
        {
            switch (Value->Type)
            {
            case EJson::Number: Type = TEXT("scalar"); break;
            case EJson::Boolean: Type = TEXT("switch"); break;
            case EJson::Array:
            case EJson::Object: Type = TEXT("vector"); break;
            case EJson::String: Type = Value->AsString().StartsWith(TEXT("#")) ? TEXT("vector") : TEXT("texture"); break;
            default: break;
            }
        }

        // En UE 5.5 las funciones Set* de UMaterialEditingLibrary siempre devuelven false,
        // así que la existencia del parámetro se comprueba aparte.
        if (Type != TEXT("scalar") && Type != TEXT("vector") && Type != TEXT("texture") && Type != TEXT("switch"))
        {
            OutError = FString::Printf(TEXT("Tipo de parámetro desconocido '%s' (scalar, vector, texture, switch)"), *Type);
            return false;
        }
        if (!HasParameter(Instance, Type, ParamName))
        {
            OutError = FString::Printf(TEXT("El parámetro %s '%s' no existe en el material padre"), *Type, *Name);
            return false;
        }

        if (Type == TEXT("scalar"))
        {
            UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, ParamName, Value->AsNumber());
        }
        else if (Type == TEXT("vector"))
        {
            FLinearColor Color;
            if (!JsonToColor(Value, Color))
            {
                OutError = TEXT("Un vector se indica como [R,G,B,A], {R,G,B,A} o \"#RRGGBB\"");
                return false;
            }
            UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(Instance, ParamName, Color);
        }
        else if (Type == TEXT("texture"))
        {
            UTexture* Texture = nullptr;
            const FString TexturePath = Value->AsString();
            if (!TexturePath.IsEmpty() && TexturePath != TEXT("None"))
            {
                Texture = Cast<UTexture>(FUnrealMCPCommonUtils::ResolveAsset(TexturePath, OutError, UTexture::StaticClass()));
                if (!Texture)
                {
                    return false;
                }
            }
            UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, ParamName, Texture);
        }
        else
        {
            UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(Instance, ParamName, Value->AsBool());
        }
        return true;
    }
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_material_instance")) return HandleCreateMaterialInstance(Params);
    if (CommandType == TEXT("set_material_instance_param")) return HandleSetMaterialInstanceParam(Params);
    if (CommandType == TEXT("get_material_params")) return HandleGetMaterialParams(Params);
    if (CommandType == TEXT("set_actor_material")) return HandleSetActorMaterial(Params);
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Comando de material desconocido: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleCreateMaterialInstance(const TSharedPtr<FJsonObject>& Params)
{
    FString Name, ParentPath;
    if (!Params->TryGetStringField(TEXT("name"), Name) || !Params->TryGetStringField(TEXT("parent"), ParentPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Faltan 'name' y 'parent'"));
    }
    FString Folder = TEXT("/Game/Materials");
    Params->TryGetStringField(TEXT("folder"), Folder);

    FString Error;
    UMaterialInterface* Parent = Cast<UMaterialInterface>(FUnrealMCPCommonUtils::ResolveAsset(ParentPath, Error, UMaterialInterface::StaticClass()));
    if (!Parent)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    const FString PackagePath = FUnrealMCPCommonUtils::MakePackagePath(Folder, Name);
    if (FPackageName::DoesPackageExist(PackagePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Ya existe %s"), *PackagePath));
    }

    UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
    Factory->InitialParent = Parent;

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(
        AssetTools.CreateAsset(Name, FPackageName::GetLongPackagePath(PackagePath), UMaterialInstanceConstant::StaticClass(), Factory));
    if (!Instance)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se pudo crear el Material Instance"));
    }

    // Parámetros iniciales opcionales: {"Color": [1,0,0,1], "Roughness": 0.4}
    const TSharedPtr<FJsonObject>* Initial = nullptr;
    TArray<FString> Warnings;
    if (Params->TryGetObjectField(TEXT("params"), Initial) && Initial)
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Initial)->Values)
        {
            if (!SetParameter(Instance, Pair.Key, Pair.Value, FString(), Error))
            {
                Warnings.Add(Error);
            }
        }
        UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), Instance->GetPathName());
    Result->SetStringField(TEXT("parent"), Parent->GetPathName());
    if (Warnings.Num() > 0)
    {
        Result->SetArrayField(TEXT("warnings"), FUnrealMCPCommonUtils::StringArrayToJson(Warnings));
    }
    Result->SetStringField(TEXT("note"), TEXT("Sin guardar: usa save_assets"));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleSetMaterialInstanceParam(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'asset_path'"));
    }

    FString Error;
    UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(
        FUnrealMCPCommonUtils::ResolveAsset(AssetPath, Error, UMaterialInstanceConstant::StaticClass()));
    if (!Instance)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    TArray<TPair<FString, TSharedPtr<FJsonValue>>> Assignments;
    FString Name;
    if (Params->TryGetStringField(TEXT("name"), Name) && Params->HasField(TEXT("value")))
    {
        Assignments.Add({ Name, Params->TryGetField(TEXT("value")) });
    }
    const TSharedPtr<FJsonObject>* Batch = nullptr;
    if (Params->TryGetObjectField(TEXT("params"), Batch) && Batch)
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Batch)->Values)
        {
            Assignments.Add(Pair);
        }
    }
    if (Assignments.Num() == 0)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Indica 'name' + 'value' o 'params': {nombre: valor}"));
    }

    FString Type;
    Params->TryGetStringField(TEXT("type"), Type);

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Set Material Parameter"));
    Instance->Modify();
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Assignment : Assignments)
    {
        if (!SetParameter(Instance, Assignment.Key, Assignment.Value, Assignments.Num() == 1 ? Type : FString(), Error))
        {
            Transaction.Cancel();
            return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
        }
    }
    UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
    Instance->MarkPackageDirty();

    return HandleGetMaterialParams(Params);
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleGetMaterialParams(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'asset_path'"));
    }

    FString Error;
    UMaterialInterface* Material = Cast<UMaterialInterface>(FUnrealMCPCommonUtils::ResolveAsset(AssetPath, Error, UMaterialInterface::StaticClass()));
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    TArray<FMaterialParameterInfo> Infos;
    TArray<FGuid> Ids;

    TSharedPtr<FJsonObject> Scalars = MakeShared<FJsonObject>();
    Material->GetAllScalarParameterInfo(Infos, Ids);
    for (const FMaterialParameterInfo& Info : Infos)
    {
        float Value = 0.f;
        Material->GetScalarParameterValue(Info, Value);
        Scalars->SetNumberField(Info.Name.ToString(), Value);
    }

    TSharedPtr<FJsonObject> Vectors = MakeShared<FJsonObject>();
    Material->GetAllVectorParameterInfo(Infos, Ids);
    for (const FMaterialParameterInfo& Info : Infos)
    {
        FLinearColor Value;
        Material->GetVectorParameterValue(Info, Value);
        Vectors->SetField(Info.Name.ToString(), ColorToJson(Value));
    }

    TSharedPtr<FJsonObject> Textures = MakeShared<FJsonObject>();
    Material->GetAllTextureParameterInfo(Infos, Ids);
    for (const FMaterialParameterInfo& Info : Infos)
    {
        UTexture* Value = nullptr;
        Material->GetTextureParameterValue(Info, Value);
        Textures->SetStringField(Info.Name.ToString(), Value ? Value->GetPathName() : TEXT("None"));
    }

    TSharedPtr<FJsonObject> Switches = MakeShared<FJsonObject>();
    Material->GetAllStaticSwitchParameterInfo(Infos, Ids);
    for (const FMaterialParameterInfo& Info : Infos)
    {
        bool Value = false;
        FGuid ExpressionId;
        Material->GetStaticSwitchParameterValue(Info, Value, ExpressionId);
        Switches->SetBoolField(Info.Name.ToString(), Value);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Result->SetStringField(TEXT("class"), Material->GetClass()->GetName());
    if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Material))
    {
        Result->SetStringField(TEXT("parent"), Instance->Parent ? Instance->Parent->GetPathName() : TEXT("None"));
    }
    if (UMaterial* Base = Material->GetMaterial())
    {
        Result->SetStringField(TEXT("base_material"), Base->GetPathName());
    }
    Result->SetObjectField(TEXT("scalar"), Scalars);
    Result->SetObjectField(TEXT("vector"), Vectors);
    Result->SetObjectField(TEXT("texture"), Textures);
    Result->SetObjectField(TEXT("switch"), Switches);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleSetActorMaterial(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName, MaterialPath;
    if (!Params->TryGetStringField(TEXT("actor"), ActorName) || !Params->TryGetStringField(TEXT("material"), MaterialPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Faltan 'actor' y 'material'"));
    }

    FString Error;
    AActor* Actor = FUnrealMCPCommonUtils::FindActor(ActorName, Error);
    if (!Actor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    UMaterialInterface* Material = Cast<UMaterialInterface>(FUnrealMCPCommonUtils::ResolveAsset(MaterialPath, Error, UMaterialInterface::StaticClass()));
    if (!Material)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    // Componente concreto o todos los mesh components del actor.
    TArray<UMeshComponent*> Targets;
    FString ComponentName;
    if (Params->TryGetStringField(TEXT("component"), ComponentName) && !ComponentName.IsEmpty())
    {
        UMeshComponent* Mesh = Cast<UMeshComponent>(FUnrealMCPCommonUtils::FindComponent(Actor, ComponentName));
        if (!Mesh)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("'%s' no es un mesh component de %s"), *ComponentName, *ActorName));
        }
        Targets.Add(Mesh);
    }
    else
    {
        Actor->GetComponents<UMeshComponent>(Targets);
    }
    if (Targets.Num() == 0)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("%s no tiene mesh components"), *ActorName));
    }

    FString SlotName;
    Params->TryGetStringField(TEXT("slot_name"), SlotName);
    int32 SlotIndex = 0;
    Params->TryGetNumberField(TEXT("slot"), SlotIndex);

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Set Actor Material"));
    TArray<FString> Changed;
    for (UMeshComponent* Mesh : Targets)
    {
        Mesh->Modify();
        if (!SlotName.IsEmpty())
        {
            const int32 Index = Mesh->GetMaterialIndex(FName(*SlotName));
            if (Index == INDEX_NONE)
            {
                continue;
            }
            Mesh->SetMaterial(Index, Material);
        }
        else
        {
            if (SlotIndex >= Mesh->GetNumMaterials())
            {
                continue;
            }
            Mesh->SetMaterial(SlotIndex, Material);
        }
        Changed.Add(Mesh->GetName());
    }

    if (Changed.Num() == 0)
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Ningún componente tiene ese slot de material"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("actor"), Actor->GetActorLabel());
    Result->SetStringField(TEXT("material"), Material->GetPathName());
    Result->SetArrayField(TEXT("components"), FUnrealMCPCommonUtils::StringArrayToJson(Changed));
    return Result;
}
