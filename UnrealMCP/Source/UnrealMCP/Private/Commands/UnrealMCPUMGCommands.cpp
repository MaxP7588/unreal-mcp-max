#include "Commands/UnrealMCPUMGCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_ComponentBoundEvent.h"
#include "EdGraphSchema_K2.h"
#include "Misc/PackageName.h"

namespace
{
    UWidgetBlueprint* FindWidgetBlueprint(const TSharedPtr<FJsonObject>& Params, FString& OutError)
    {
        FString Name;
        if (!Params->TryGetStringField(TEXT("blueprint_name"), Name) && !Params->TryGetStringField(TEXT("blueprint_path"), Name))
        {
            OutError = TEXT("Falta 'blueprint_name' (nombre corto o ruta /Game/...)");
            return nullptr;
        }
        return Cast<UWidgetBlueprint>(FUnrealMCPCommonUtils::ResolveAsset(Name, OutError, UWidgetBlueprint::StaticClass()));
    }

    UClass* ResolveWidgetClass(const FString& TypeName, FString& OutError)
    {
        if (TypeName.StartsWith(TEXT("/Game/")))
        {
            if (UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FUnrealMCPCommonUtils::ResolveAsset(TypeName, OutError, UWidgetBlueprint::StaticClass())))
            {
                return Blueprint->GeneratedClass;
            }
            return nullptr;
        }
        UClass* Class = FUnrealMCPCommonUtils::ResolveClass(TypeName, OutError);
        if (Class && !Class->IsChildOf(UWidget::StaticClass()))
        {
            OutError = FString::Printf(TEXT("%s no es un widget"), *Class->GetName());
            return nullptr;
        }
        return Class;
    }

    void FinishEdit(UWidgetBlueprint* WidgetBlueprint)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);
        FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
        WidgetBlueprint->MarkPackageDirty();
    }

    /** Aplica {"Ruta": valor} sobre un objeto. */
    bool ApplyProperties(UObject* Target, const TSharedPtr<FJsonObject>& Properties, FString& OutError)
    {
        if (!Target || !Properties.IsValid())
        {
            return true;
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
        {
            if (!FUnrealMCPCommonUtils::SetObjectPropertyByPath(Target, Pair.Key, Pair.Value, OutError))
            {
                OutError = FString::Printf(TEXT("%s.%s: %s"), *Target->GetName(), *Pair.Key, *OutError);
                return false;
            }
        }
        return true;
    }

    /** Atajos de slot: position/size/alignment/z_order en canvas; el resto por reflexión. */
    bool ApplySlot(UPanelSlot* Slot, const TSharedPtr<FJsonObject>& Params, FString& OutError)
    {
        if (!Slot)
        {
            return true;
        }
        if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
        {
            if (Params->HasField(TEXT("position")))
            {
                CanvasSlot->SetPosition(FUnrealMCPCommonUtils::GetVector2DFromJson(Params, TEXT("position")));
            }
            if (Params->HasField(TEXT("size")))
            {
                CanvasSlot->SetSize(FUnrealMCPCommonUtils::GetVector2DFromJson(Params, TEXT("size")));
            }
            if (Params->HasField(TEXT("alignment")))
            {
                CanvasSlot->SetAlignment(FUnrealMCPCommonUtils::GetVector2DFromJson(Params, TEXT("alignment")));
            }
            bool bAutoSize = false;
            if (Params->TryGetBoolField(TEXT("auto_size"), bAutoSize))
            {
                CanvasSlot->SetAutoSize(bAutoSize);
            }
            int32 ZOrder = 0;
            if (Params->TryGetNumberField(TEXT("z_order"), ZOrder))
            {
                CanvasSlot->SetZOrder(ZOrder);
            }
        }
        const TSharedPtr<FJsonObject>* SlotProperties = nullptr;
        if (Params->TryGetObjectField(TEXT("slot"), SlotProperties) && SlotProperties)
        {
            return ApplyProperties(Slot, *SlotProperties, OutError);
        }
        return true;
    }

    TSharedPtr<FJsonObject> WidgetToJson(UWidget* Widget)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Widget->GetName());
        Entry->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
        Entry->SetBoolField(TEXT("is_variable"), Widget->bIsVariable);
        if (UPanelSlot* Slot = Widget->Slot)
        {
            Entry->SetStringField(TEXT("slot"), Slot->GetClass()->GetName());
            if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
            {
                const FVector2D Position = CanvasSlot->GetPosition();
                const FVector2D Size = CanvasSlot->GetSize();
                Entry->SetArrayField(TEXT("position"), { MakeShared<FJsonValueNumber>(Position.X), MakeShared<FJsonValueNumber>(Position.Y) });
                Entry->SetArrayField(TEXT("size"), { MakeShared<FJsonValueNumber>(Size.X), MakeShared<FJsonValueNumber>(Size.Y) });
            }
        }
        if (UTextBlock* Text = Cast<UTextBlock>(Widget))
        {
            Entry->SetStringField(TEXT("text"), Text->GetText().ToString());
        }
        if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
        {
            TArray<TSharedPtr<FJsonValue>> Children;
            for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
            {
                if (UWidget* Child = Panel->GetChildAt(Index))
                {
                    Children.Add(MakeShared<FJsonValueObject>(WidgetToJson(Child)));
                }
            }
            Entry->SetArrayField(TEXT("children"), Children);
        }
        return Entry;
    }
}

FUnrealMCPUMGCommands::FUnrealMCPUMGCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleCommand(const FString& CommandName, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandName == TEXT("create_umg_widget_blueprint")) return HandleCreateUMGWidgetBlueprint(Params);
    if (CommandName == TEXT("add_widget")) return HandleAddWidget(Params);
    if (CommandName == TEXT("get_widget_tree")) return HandleGetWidgetTree(Params);
    if (CommandName == TEXT("set_widget_property")) return HandleSetWidgetProperty(Params);
    if (CommandName == TEXT("add_text_block_to_widget")) return HandleAddTextBlockToWidget(Params);
    if (CommandName == TEXT("add_button_to_widget")) return HandleAddButtonToWidget(Params);
    if (CommandName == TEXT("add_widget_to_viewport")) return HandleAddWidgetToViewport(Params);
    if (CommandName == TEXT("bind_widget_event")) return HandleBindWidgetEvent(Params);
    if (CommandName == TEXT("set_text_block_binding")) return HandleSetTextBlockBinding(Params);

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Comando UMG desconocido: %s"), *CommandName));
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleCreateUMGWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("name"), BlueprintName) || BlueprintName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'name'"));
    }
    FString Folder = TEXT("/Game/UI");
    Params->TryGetStringField(TEXT("path"), Folder);

    const FString PackagePath = FUnrealMCPCommonUtils::MakePackagePath(Folder, BlueprintName);
    if (FPackageName::DoesPackageExist(PackagePath) || FindObject<UPackage>(nullptr, *PackagePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Ya existe %s"), *PackagePath));
    }

    FString Error;
    UClass* ParentClass = UUserWidget::StaticClass();
    FString ParentName;
    if (Params->TryGetStringField(TEXT("parent_class"), ParentName) && !ParentName.IsEmpty())
    {
        ParentClass = ResolveWidgetClass(ParentName, Error);
        if (!ParentClass || !ParentClass->IsChildOf(UUserWidget::StaticClass()))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(Error.IsEmpty() ? TEXT("parent_class debe heredar de UserWidget") : Error);
        }
    }

    UPackage* Package = CreatePackage(*PackagePath);
    UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        ParentClass, Package, FName(*BlueprintName), BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass(), FName("UnrealMCP")));
    if (!WidgetBlueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se pudo crear el Widget Blueprint"));
    }

    FString RootType = TEXT("CanvasPanel");
    Params->TryGetStringField(TEXT("root_widget"), RootType);
    if (!RootType.IsEmpty() && !WidgetBlueprint->WidgetTree->RootWidget)
    {
        UClass* RootClass = ResolveWidgetClass(RootType, Error);
        if (RootClass)
        {
            WidgetBlueprint->WidgetTree->RootWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(RootClass, FName(*RootType));
        }
    }

    FAssetRegistryModule::AssetCreated(WidgetBlueprint);
    FinishEdit(WidgetBlueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("name"), BlueprintName);
    ResultObj->SetStringField(TEXT("path"), WidgetBlueprint->GetPathName());
    ResultObj->SetStringField(TEXT("note"), TEXT("Sin guardar: usa save_assets"));
    return ResultObj;
}

UWidget* FUnrealMCPUMGCommands::AddWidgetInternal(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetType, const FString& WidgetName,
                                                   const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
    UWidgetTree* Tree = WidgetBlueprint->WidgetTree;
    if (Tree->FindWidget(FName(*WidgetName)))
    {
        OutError = FString::Printf(TEXT("Ya existe un widget llamado %s"), *WidgetName);
        return nullptr;
    }

    UClass* WidgetClass = ResolveWidgetClass(WidgetType, OutError);
    if (!WidgetClass)
    {
        return nullptr;
    }

    // Padre: el indicado o el root. Sin root, el widget nuevo pasa a ser el root.
    UPanelWidget* Parent = nullptr;
    FString ParentName;
    if (Params->TryGetStringField(TEXT("parent"), ParentName) && !ParentName.IsEmpty())
    {
        Parent = Cast<UPanelWidget>(Tree->FindWidget(FName(*ParentName)));
        if (!Parent)
        {
            OutError = FString::Printf(TEXT("'%s' no existe o no es un panel"), *ParentName);
            return nullptr;
        }
    }
    else if (Tree->RootWidget)
    {
        Parent = Cast<UPanelWidget>(Tree->RootWidget);
        if (!Parent)
        {
            OutError = TEXT("El root no es un panel; indica 'parent'");
            return nullptr;
        }
    }
    if (Parent && !Parent->CanAddMoreChildren())
    {
        OutError = FString::Printf(TEXT("%s no admite más hijos"), *Parent->GetName());
        return nullptr;
    }

    WidgetBlueprint->Modify();
    Tree->Modify();
    UWidget* Widget = Tree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetName));
    if (!Widget)
    {
        OutError = TEXT("ConstructWidget falló");
        return nullptr;
    }

    bool bIsVariable = true;
    Params->TryGetBoolField(TEXT("is_variable"), bIsVariable);
    Widget->bIsVariable = bIsVariable;

    UPanelSlot* Slot = nullptr;
    if (Parent)
    {
        Parent->Modify();
        Slot = Parent->AddChild(Widget);
    }
    else
    {
        Tree->RootWidget = Widget;
    }

    FString Text;
    if (Params->TryGetStringField(TEXT("text"), Text))
    {
        if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
        {
            TextBlock->SetText(FText::FromString(Text));
        }
    }

    const TSharedPtr<FJsonObject>* Properties = nullptr;
    if (Params->TryGetObjectField(TEXT("properties"), Properties) && Properties && !ApplyProperties(Widget, *Properties, OutError))
    {
        return nullptr;
    }
    if (!ApplySlot(Slot, Params, OutError))
    {
        return nullptr;
    }
    return Widget;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddWidget(const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetType, WidgetName, Error;
    if (!Params->TryGetStringField(TEXT("widget_type"), WidgetType) || !Params->TryGetStringField(TEXT("widget_name"), WidgetName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Faltan 'widget_type' (TextBlock, Button, Image, VerticalBox, ...) y 'widget_name'"));
    }
    UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(Params, Error);
    if (!WidgetBlueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Add Widget"));
    UWidget* Widget = AddWidgetInternal(WidgetBlueprint, WidgetType, WidgetName, Params, Error);
    if (!Widget)
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    FinishEdit(WidgetBlueprint);

    TSharedPtr<FJsonObject> Result = WidgetToJson(Widget);
    Result->SetStringField(TEXT("blueprint"), WidgetBlueprint->GetPathName());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleGetWidgetTree(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(Params, Error);
    if (!WidgetBlueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint"), WidgetBlueprint->GetPathName());
    Result->SetStringField(TEXT("parent_class"), WidgetBlueprint->ParentClass ? WidgetBlueprint->ParentClass->GetName() : TEXT("None"));
    if (UWidget* Root = WidgetBlueprint->WidgetTree->RootWidget)
    {
        Result->SetObjectField(TEXT("root"), WidgetToJson(Root));
    }

    TArray<TSharedPtr<FJsonValue>> Bindings;
    for (const FDelegateEditorBinding& Binding : WidgetBlueprint->Bindings)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("widget"), Binding.ObjectName);
        Entry->SetStringField(TEXT("property"), Binding.PropertyName.ToString());
        Entry->SetStringField(TEXT("function"), Binding.FunctionName.ToString());
        Entry->SetStringField(TEXT("source_property"), Binding.SourceProperty.ToString());
        Bindings.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetArrayField(TEXT("bindings"), Bindings);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleSetWidgetProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetName, Error;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'widget_name'"));
    }
    UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(Params, Error);
    if (!WidgetBlueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
    if (!Widget)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No existe el widget %s"), *WidgetName));
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Set Widget Property"));
    WidgetBlueprint->Modify();
    const TSharedPtr<FJsonObject>* Properties = nullptr;
    if (Params->TryGetObjectField(TEXT("properties"), Properties) && Properties && !ApplyProperties(Widget, *Properties, Error))
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    FString Text;
    if (Params->TryGetStringField(TEXT("text"), Text))
    {
        if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
        {
            TextBlock->SetText(FText::FromString(Text));
        }
    }
    if (!ApplySlot(Widget->Slot, Params, Error))
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    FinishEdit(WidgetBlueprint);

    return WidgetToJson(Widget);
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddTextBlockToWidget(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Forward = MakeShared<FJsonObject>(*Params);
    Forward->SetStringField(TEXT("widget_type"), TEXT("TextBlock"));
    if (!Forward->HasField(TEXT("text")))
    {
        Forward->SetStringField(TEXT("text"), TEXT("New Text Block"));
    }
    return HandleAddWidget(Forward);
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddButtonToWidget(const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetName, Error;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'widget_name'"));
    }
    UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(Params, Error);
    if (!WidgetBlueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Add Button"));
    TSharedPtr<FJsonObject> ButtonParams = MakeShared<FJsonObject>(*Params);
    ButtonParams->RemoveField(TEXT("text"));
    UWidget* Button = AddWidgetInternal(WidgetBlueprint, TEXT("Button"), WidgetName, ButtonParams, Error);
    if (!Button)
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    // El texto del botón es un TextBlock hijo.
    FString ButtonText;
    if (Params->TryGetStringField(TEXT("text"), ButtonText))
    {
        TSharedPtr<FJsonObject> TextParams = MakeShared<FJsonObject>();
        TextParams->SetStringField(TEXT("parent"), WidgetName);
        TextParams->SetStringField(TEXT("text"), ButtonText);
        TextParams->SetBoolField(TEXT("is_variable"), false);
        if (!AddWidgetInternal(WidgetBlueprint, TEXT("TextBlock"), WidgetName + TEXT("_Text"), TextParams, Error))
        {
            Transaction.Cancel();
            return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
        }
    }
    FinishEdit(WidgetBlueprint);

    TSharedPtr<FJsonObject> Result = WidgetToJson(Button);
    Result->SetStringField(TEXT("blueprint"), WidgetBlueprint->GetPathName());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleAddWidgetToViewport(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(Params, Error);
    if (!WidgetBlueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    if (!WidgetBlueprint->GeneratedClass)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("El Widget Blueprint no tiene clase generada (compílalo)"));
    }

    // Solo tiene sentido con PIE corriendo: crea el widget en el mundo de juego.
    UWorld* PlayWorld = GEditor->PlayWorld;
    if (!PlayWorld)
    {
        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("class_path"), WidgetBlueprint->GeneratedClass->GetPathName());
        ResultObj->SetStringField(TEXT("note"), TEXT("PIE no está corriendo. Usa CreateWidget + AddToViewport en un Blueprint, o llama esto con PIE activo."));
        return ResultObj;
    }

    int32 ZOrder = 0;
    Params->TryGetNumberField(TEXT("z_order"), ZOrder);
    UUserWidget* Instance = CreateWidget<UUserWidget>(PlayWorld, WidgetBlueprint->GeneratedClass.Get());
    if (!Instance)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("CreateWidget falló"));
    }
    Instance->AddToViewport(ZOrder);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("instance"), Instance->GetName());
    ResultObj->SetNumberField(TEXT("z_order"), ZOrder);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleBindWidgetEvent(const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetName, EventName, Error;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName) || !Params->TryGetStringField(TEXT("event_name"), EventName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Faltan 'widget_name' y 'event_name' (p. ej. OnClicked)"));
    }
    UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(Params, Error);
    if (!WidgetBlueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
    if (!Widget)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No existe el widget %s"), *WidgetName));
    }

    // El evento se enlaza a la variable del widget, que solo existe si bIsVariable y tras compilar.
    if (!Widget->bIsVariable)
    {
        Widget->bIsVariable = true;
        FinishEdit(WidgetBlueprint);
    }

    if (!FindFProperty<FMulticastDelegateProperty>(Widget->GetClass(), FName(*EventName)))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("%s no tiene el evento %s"), *Widget->GetClass()->GetName(), *EventName));
    }

    FObjectProperty* WidgetProperty = FindFProperty<FObjectProperty>(WidgetBlueprint->SkeletonGeneratedClass, FName(*WidgetName));
    if (!WidgetProperty)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se encontró la variable del widget en la clase compilada"));
    }

    const UK2Node_ComponentBoundEvent* Existing = FKismetEditorUtilities::FindBoundEventForComponent(WidgetBlueprint, FName(*EventName), WidgetProperty->GetFName());
    bool bCreated = false;
    if (!Existing)
    {
        FUnrealMCPEditTransaction Transaction(TEXT("MCP: Bind Widget Event"));
        FKismetEditorUtilities::CreateNewBoundEventForClass(Widget->GetClass(), FName(*EventName), WidgetBlueprint, WidgetProperty);
        Existing = FKismetEditorUtilities::FindBoundEventForComponent(WidgetBlueprint, FName(*EventName), WidgetProperty->GetFName());
        bCreated = Existing != nullptr;
        FinishEdit(WidgetBlueprint);
    }
    if (!Existing)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se pudo crear el nodo del evento"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("event_name"), EventName);
    Result->SetStringField(TEXT("widget"), WidgetName);
    Result->SetStringField(TEXT("node_id"), Existing->NodeGuid.ToString());
    Result->SetBoolField(TEXT("created"), bCreated);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPUMGCommands::HandleSetTextBlockBinding(const TSharedPtr<FJsonObject>& Params)
{
    FString WidgetName, BindingName, Error;
    if (!Params->TryGetStringField(TEXT("widget_name"), WidgetName) || !Params->TryGetStringField(TEXT("binding_name"), BindingName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Faltan 'widget_name' y 'binding_name'"));
    }
    UWidgetBlueprint* WidgetBlueprint = FindWidgetBlueprint(Params, Error);
    if (!WidgetBlueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    if (!Cast<UTextBlock>(WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName))))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("%s no es un TextBlock"), *WidgetName));
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Text Binding"));
    WidgetBlueprint->Modify();

    // Variable Text que alimenta la propiedad Text del TextBlock (binding de propiedad, sin función).
    if (FBlueprintEditorUtils::FindNewVariableIndex(WidgetBlueprint, FName(*BindingName)) == INDEX_NONE)
    {
        FEdGraphPinType TextType;
        TextType.PinCategory = UEdGraphSchema_K2::PC_Text;
        FBlueprintEditorUtils::AddMemberVariable(WidgetBlueprint, FName(*BindingName), TextType);
    }

    WidgetBlueprint->Bindings.RemoveAll([&WidgetName](const FDelegateEditorBinding& Binding)
    {
        return Binding.ObjectName == WidgetName && Binding.PropertyName == TEXT("Text");
    });
    FDelegateEditorBinding Binding;
    Binding.ObjectName = WidgetName;
    Binding.PropertyName = TEXT("Text");
    Binding.SourceProperty = FName(*BindingName);
    Binding.Kind = EBindingKind::Property;
    WidgetBlueprint->Bindings.Add(Binding);

    FinishEdit(WidgetBlueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("widget"), WidgetName);
    Result->SetStringField(TEXT("binding_variable"), BindingName);
    return Result;
}
