#include "Commands/UnrealMCPGraphSerializer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/World.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Composite.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Timeline.h"
#include "K2Node_Variable.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"

namespace UnrealMCPGraph
{
    namespace
    {
        FString EnabledStateToString(ENodeEnabledState State)
        {
            switch (State)
            {
            case ENodeEnabledState::Disabled:
                return TEXT("Disabled");
            case ENodeEnabledState::DevelopmentOnly:
                return TEXT("DevelopmentOnly");
            default:
                return TEXT("Enabled");
            }
        }

        FString EnumLabelForPin(const UEdGraphPin* Pin)
        {
            UEnum* Enum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get());
            if (!Enum)
            {
                return FString();
            }

            const FName& Category = Pin->PinType.PinCategory;
            const bool bEnumCategory = Category == UEdGraphSchema_K2::PC_Byte
                || Category == UEdGraphSchema_K2::PC_Enum
                || Category == UEdGraphSchema_K2::PC_Int;
            if (!bEnumCategory)
            {
                return FString();
            }

            FString Raw = Pin->GetDefaultAsString();
            if (Raw.IsEmpty())
            {
                return FString();
            }

            FString ShortName = Raw;
            int32 Separator = INDEX_NONE;
            if (Raw.FindLastChar(TEXT(':'), Separator))
            {
                ShortName = Raw.Mid(Separator + 1);
            }

            int64 Value = Enum->GetValueByNameString(ShortName);
            if (Value == INDEX_NONE)
            {
                Value = Enum->GetValueByNameString(Raw);
            }
            if (Value == INDEX_NONE)
            {
                return FString();
            }

            return Enum->GetDisplayNameTextByValue(Value).ToString();
        }

        bool IsWorldAsset(const FString& ObjectPath)
        {
            FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
            const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
            return AssetData.IsValid() && AssetData.AssetClassPath.GetAssetName() == TEXT("World");
        }

        TSharedPtr<FJsonObject> NodeToJsonInternal(UEdGraphNode* Node, TSet<const UEdGraph*>& Visiting);
        TSharedPtr<FJsonObject> GraphToJsonInternal(UEdGraph* Graph, TSet<const UEdGraph*>& Visiting);

        TSharedPtr<FJsonObject> GraphToJsonInternal(UEdGraph* Graph, TSet<const UEdGraph*>& Visiting)
        {
            if (!Graph)
            {
                return nullptr;
            }

            if (Visiting.Contains(Graph))
            {
                TSharedPtr<FJsonObject> Stub = MakeShared<FJsonObject>();
                Stub->SetStringField(TEXT("name"), Graph->GetName());
                Stub->SetBoolField(TEXT("truncated"), true);
                return Stub;
            }

            Visiting.Add(Graph);

            TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
            GraphObj->SetStringField(TEXT("name"), Graph->GetName());
            GraphObj->SetStringField(TEXT("schema"), Graph->GetSchema() ? Graph->GetSchema()->GetClass()->GetName() : TEXT(""));

            TArray<TSharedPtr<FJsonValue>> Nodes;
            TArray<TSharedPtr<FJsonValue>> Links;
            TArray<TSharedPtr<FJsonValue>> Comments;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node)
                {
                    continue;
                }

                if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
                {
                    TSharedPtr<FJsonObject> CommentObj = MakeShared<FJsonObject>();
                    CommentObj->SetStringField(TEXT("id"), Comment->NodeGuid.ToString(EGuidFormats::Digits));
                    CommentObj->SetStringField(TEXT("text"), Comment->NodeComment);

                    const float Left = Comment->NodePosX;
                    const float Top = Comment->NodePosY;
                    const float Right = Left + Comment->NodeWidth;
                    const float Bottom = Top + Comment->NodeHeight;

                    TArray<TSharedPtr<FJsonValue>> Contained;
                    for (UEdGraphNode* Other : Graph->Nodes)
                    {
                        if (!Other || Other == Comment || Cast<UEdGraphNode_Comment>(Other))
                        {
                            continue;
                        }

                        if (Other->NodePosX >= Left && Other->NodePosX <= Right
                            && Other->NodePosY >= Top && Other->NodePosY <= Bottom)
                        {
                            Contained.Add(MakeShared<FJsonValueString>(Other->NodeGuid.ToString(EGuidFormats::Digits)));
                        }
                    }
                    CommentObj->SetArrayField(TEXT("nodes"), Contained);
                    Comments.Add(MakeShared<FJsonValueObject>(CommentObj));
                    continue;
                }

                Nodes.Add(MakeShared<FJsonValueObject>(NodeToJsonInternal(Node, Visiting)));
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (!Pin || Pin->Direction != EGPD_Output)
                    {
                        continue;
                    }

                    for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                    {
                        if (!LinkedPin || !LinkedPin->GetOwningNode())
                        {
                            continue;
                        }

                        TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
                        LinkObj->SetStringField(TEXT("from_node"), Node->NodeGuid.ToString(EGuidFormats::Digits));
                        LinkObj->SetStringField(TEXT("from_pin"), Pin->PinName.ToString());
                        LinkObj->SetStringField(TEXT("to_node"), LinkedPin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::Digits));
                        LinkObj->SetStringField(TEXT("to_pin"), LinkedPin->PinName.ToString());
                        Links.Add(MakeShared<FJsonValueObject>(LinkObj));
                    }
                }
            }

            GraphObj->SetArrayField(TEXT("nodes"), Nodes);
            GraphObj->SetArrayField(TEXT("links"), Links);
            GraphObj->SetArrayField(TEXT("comments"), Comments);
            return GraphObj;
        }

        TSharedPtr<FJsonObject> NodeToJsonInternal(UEdGraphNode* Node, TSet<const UEdGraph*>& Visiting)
        {
            TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
            NodeObj->SetStringField(TEXT("id"), Node->NodeGuid.ToString(EGuidFormats::Digits));
            NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
            NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            NodeObj->SetNumberField(TEXT("x"), Node->NodePosX);
            NodeObj->SetNumberField(TEXT("y"), Node->NodePosY);
            NodeObj->SetStringField(TEXT("enabled"), EnabledStateToString(Node->GetDesiredEnabledState()));

            if (!Node->NodeComment.IsEmpty())
            {
                NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);
            }

            if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
            {
                NodeObj->SetStringField(TEXT("function"), CallNode->FunctionReference.GetMemberName().ToString());
            }

            if (UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
            {
                NodeObj->SetStringField(TEXT("custom_event"), CustomEvent->CustomFunctionName.ToString());
            }
            else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
            {
                const FString EventName = EventNode->EventReference.GetMemberName().ToString();
                if (!EventName.IsEmpty())
                {
                    NodeObj->SetStringField(TEXT("event"), EventName);
                }
            }

            if (UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node))
            {
                NodeObj->SetStringField(TEXT("variable"), VariableNode->GetVarNameString());
                if (UClass* Owner = VariableNode->VariableReference.GetMemberParentClass())
                {
                    NodeObj->SetStringField(TEXT("variable_owner"), Owner->GetName());
                }
            }

            if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
            {
                if (CastNode->TargetType)
                {
                    NodeObj->SetStringField(TEXT("cast_target"), CastNode->TargetType->GetName());
                }
            }

            if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
            {
                if (UEdGraph* MacroGraph = MacroNode->GetMacroGraph())
                {
                    NodeObj->SetStringField(TEXT("macro"), MacroGraph->GetName());
                }
            }

            if (UK2Node_ComponentBoundEvent* BoundEvent = Cast<UK2Node_ComponentBoundEvent>(Node))
            {
                NodeObj->SetStringField(TEXT("component"), BoundEvent->ComponentPropertyName.ToString());
                NodeObj->SetStringField(TEXT("delegate"), BoundEvent->DelegatePropertyName.ToString());
            }

            if (UK2Node_Timeline* Timeline = Cast<UK2Node_Timeline>(Node))
            {
                NodeObj->SetStringField(TEXT("timeline"), Timeline->TimelineName.ToString());
            }

            if (UK2Node_CreateDelegate* CreateDelegate = Cast<UK2Node_CreateDelegate>(Node))
            {
                NodeObj->SetStringField(TEXT("delegate_function"), CreateDelegate->GetFunctionName().ToString());
            }

            if (UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node))
            {
                if (Composite->BoundGraph)
                {
                    NodeObj->SetStringField(TEXT("collapsed_graph"), Composite->BoundGraph->GetName());
                    if (TSharedPtr<FJsonObject> Bound = GraphToJsonInternal(Composite->BoundGraph, Visiting))
                    {
                        NodeObj->SetObjectField(TEXT("bound_graph"), Bound);
                    }
                }
            }

            TArray<TSharedPtr<FJsonValue>> Pins;
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin)
                {
                    Pins.Add(MakeShared<FJsonValueObject>(PinToJson(Pin)));
                }
            }
            NodeObj->SetArrayField(TEXT("pins"), Pins);
            return NodeObj;
        }
    }

    FString NormalizeObjectPath(const FString& InPath)
    {
        FString Path = InPath;
        Path.TrimStartAndEndInline();
        if (Path.IsEmpty() || Path.Contains(TEXT(".")))
        {
            return Path;
        }

        return Path + TEXT(".") + FPackageName::GetShortName(Path);
    }

    bool IsOutputPathInsideProject(const FString& FullPath)
    {
        FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        FPaths::CollapseRelativeDirectories(ProjectDir);
        return FPaths::IsUnderDirectory(FullPath, ProjectDir);
    }

    UBlueprint* ResolveBlueprint(const FString& ObjectPath, FString& OutError)
    {
        if (IsWorldAsset(ObjectPath))
        {
            if (!GEditor)
            {
                OutError = TEXT("No hay editor activo para leer el Level Blueprint");
                return nullptr;
            }

            UWorld* World = GEditor->GetEditorWorldContext().World();
            const FString RequestedPackage = FPackageName::ObjectPathToPackageName(ObjectPath);
            if (!World || World->GetOutermost()->GetName() != RequestedPackage)
            {
                OutError = FString::Printf(TEXT("El mapa %s no es el que está abierto. Ábrelo en el editor y vuelve a pedir el volcado."), *RequestedPackage);
                return nullptr;
            }

            if (!World->PersistentLevel)
            {
                OutError = TEXT("El mapa abierto no tiene nivel persistente");
                return nullptr;
            }

            UBlueprint* LevelBlueprint = World->PersistentLevel->GetLevelScriptBlueprint(true);
            if (!LevelBlueprint)
            {
                OutError = TEXT("El mapa abierto no tiene Level Blueprint");
            }
            return LevelBlueprint;
        }

        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
        if (!Blueprint)
        {
            OutError = FString::Printf(TEXT("No se encontró el Blueprint: %s"), *ObjectPath);
        }
        return Blueprint;
    }

    void CollectBlueprintGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs)
    {
        if (!Blueprint)
        {
            return;
        }

        auto AddUnique = [&OutGraphs](UEdGraph* Graph)
        {
            if (Graph && !OutGraphs.Contains(Graph))
            {
                OutGraphs.Add(Graph);
            }
        };

        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            AddUnique(Graph);
        }
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            AddUnique(Graph);
        }
        for (UEdGraph* Graph : Blueprint->MacroGraphs)
        {
            AddUnique(Graph);
        }
        for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
        {
            AddUnique(Graph);
        }
        for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
        {
            for (UEdGraph* Graph : InterfaceDesc.Graphs)
            {
                AddUnique(Graph);
            }
        }
    }

    TSharedPtr<FJsonObject> PinToJson(const UEdGraphPin* Pin)
    {
        TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
        PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
        PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"));
        PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
        PinObj->SetBoolField(TEXT("linked"), Pin->LinkedTo.Num() > 0);

        if (Pin->PinType.ContainerType == EPinContainerType::Array)
        {
            PinObj->SetStringField(TEXT("container"), TEXT("array"));
        }
        else if (Pin->PinType.ContainerType == EPinContainerType::Set)
        {
            PinObj->SetStringField(TEXT("container"), TEXT("set"));
        }
        else if (Pin->PinType.ContainerType == EPinContainerType::Map)
        {
            PinObj->SetStringField(TEXT("container"), TEXT("map"));
        }

        if (Pin->PinType.PinSubCategoryObject.IsValid())
        {
            PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategoryObject->GetName());
        }
        else if (!Pin->PinType.PinSubCategory.IsNone())
        {
            PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategory.ToString());
        }

        const FString DefaultValue = Pin->GetDefaultAsString();
        if (!DefaultValue.IsEmpty())
        {
            PinObj->SetStringField(TEXT("default"), DefaultValue.Left(500));
        }

        const FString EnumLabel = EnumLabelForPin(Pin);
        if (!EnumLabel.IsEmpty())
        {
            PinObj->SetStringField(TEXT("default_label"), EnumLabel);
        }

        if (Pin->DefaultObject)
        {
            PinObj->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetName());
            PinObj->SetStringField(TEXT("default_object_path"), Pin->DefaultObject->GetPathName());
        }

        return PinObj;
    }

    TSharedPtr<FJsonObject> NodeToJson(UEdGraphNode* Node)
    {
        TSet<const UEdGraph*> Visiting;
        return NodeToJsonInternal(Node, Visiting);
    }

    TSharedPtr<FJsonObject> GraphToJson(UEdGraph* Graph)
    {
        TSet<const UEdGraph*> Visiting;
        return GraphToJsonInternal(Graph, Visiting);
    }
}
