#include "Commands/UnrealMCPBlueprintReadCommands.h"

#include "Commands/UnrealMCPCommonUtils.h"
#include "Commands/UnrealMCPGraphSerializer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DataTable.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/UserDefinedEnum.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "JsonObjectConverter.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Tunnel.h"
#include "K2Node_Variable.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/SoftObjectPath.h"

namespace
{
    int32 ReadIntParam(const TSharedPtr<FJsonObject>& Params, const TCHAR* Name, int32 DefaultValue)
    {
        if (!Params.IsValid() || !Params->HasField(Name))
        {
            return DefaultValue;
        }

        double Value = 0.0;
        if (!Params->TryGetNumberField(Name, Value))
        {
            return DefaultValue;
        }

        return FMath::RoundToInt(Value);
    }

    FString JsonString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
    {
        FString Value;
        if (Object.IsValid())
        {
            Object->TryGetStringField(Field, Value);
        }
        return Value;
    }

    FString FirstTitleLine(const FString& Title)
    {
        FString First = Title;
        int32 Newline = INDEX_NONE;
        if (First.FindChar(TEXT('\n'), Newline))
        {
            First = First.Left(Newline);
        }
        First.TrimStartAndEndInline();
        return First;
    }

    FString ShortRegistryTag(const FString& Raw)
    {
        FString Inner = Raw;
        int32 Quote = INDEX_NONE;
        if (Inner.FindChar(TEXT('\''), Quote))
        {
            Inner = Inner.Mid(Quote + 1);
            Inner.RemoveFromEnd(TEXT("'"));
        }
        if (Inner.IsEmpty())
        {
            return Raw;
        }
        return FPackageName::ObjectPathToObjectName(Inner);
    }

    FString StatusToString(EBlueprintStatus Status)
    {
        switch (Status)
        {
        case BS_Dirty:
            return TEXT("Dirty");
        case BS_Error:
            return TEXT("Error");
        case BS_UpToDate:
            return TEXT("UpToDate");
        case BS_BeingCreated:
            return TEXT("BeingCreated");
        case BS_UpToDateWithWarnings:
            return TEXT("UpToDateWithWarnings");
        default:
            return TEXT("Unknown");
        }
    }

    FString SeverityToString(int32 ErrorType)
    {
        if (ErrorType <= EMessageSeverity::Error)
        {
            return TEXT("error");
        }
        if (ErrorType <= EMessageSeverity::Warning)
        {
            return TEXT("warning");
        }
        return TEXT("info");
    }

    FString GraphKind(UBlueprint* Blueprint, UEdGraph* Graph)
    {
        if (!Blueprint || !Graph)
        {
            return TEXT("unknown");
        }
        if (Blueprint->UbergraphPages.Contains(Graph))
        {
            return TEXT("event");
        }
        if (Blueprint->FunctionGraphs.Contains(Graph))
        {
            return TEXT("function");
        }
        if (Blueprint->MacroGraphs.Contains(Graph))
        {
            return TEXT("macro");
        }
        if (Blueprint->DelegateSignatureGraphs.Contains(Graph))
        {
            return TEXT("delegate");
        }
        return TEXT("interface");
    }

    void CollectCollapsedGraphs(UEdGraph* Graph, TArray<UEdGraph*>& OutGraphs, TSet<UEdGraph*>& Seen)
    {
        if (!Graph || Seen.Contains(Graph))
        {
            return;
        }
        Seen.Add(Graph);

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node);
            if (Composite && Composite->BoundGraph && !Seen.Contains(Composite->BoundGraph))
            {
                OutGraphs.Add(Composite->BoundGraph);
                CollectCollapsedGraphs(Composite->BoundGraph, OutGraphs, Seen);
            }
        }
    }

    UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName, bool& bOutCollapsed)
    {
        bOutCollapsed = false;
        TArray<UEdGraph*> Graphs;
        UnrealMCPGraph::CollectBlueprintGraphs(Blueprint, Graphs);

        for (UEdGraph* Graph : Graphs)
        {
            if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
            {
                return Graph;
            }
        }

        TArray<UEdGraph*> Collapsed;
        TSet<UEdGraph*> Seen;
        for (UEdGraph* Graph : Graphs)
        {
            CollectCollapsedGraphs(Graph, Collapsed, Seen);
        }
        for (UEdGraph* Graph : Collapsed)
        {
            if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
            {
                bOutCollapsed = true;
                return Graph;
            }
        }
        return nullptr;
    }

    TArray<TSharedPtr<FJsonValue>> DataPinsJson(UEdGraphNode* Node, EEdGraphPinDirection Direction)
    {
        TArray<TSharedPtr<FJsonValue>> Pins;
        if (!Node)
        {
            return Pins;
        }

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin->Direction != Direction || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                continue;
            }

            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
            PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
            PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
            if (Pin->PinType.PinSubCategoryObject.IsValid())
            {
                PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategoryObject->GetName());
            }
            Pins.Add(MakeShared<FJsonValueObject>(PinObj));
        }
        return Pins;
    }

    void AddContainerField(TSharedPtr<FJsonObject> Object, const FEdGraphPinType& PinType)
    {
        if (PinType.ContainerType == EPinContainerType::Array)
        {
            Object->SetStringField(TEXT("container"), TEXT("array"));
        }
        else if (PinType.ContainerType == EPinContainerType::Set)
        {
            Object->SetStringField(TEXT("container"), TEXT("set"));
        }
        else if (PinType.ContainerType == EPinContainerType::Map)
        {
            Object->SetStringField(TEXT("container"), TEXT("map"));
        }
    }

    FString EnumLabelForExported(const FProperty* Property, const FString& Exported)
    {
        const UEnum* Enum = nullptr;
        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
        {
            Enum = EnumProperty->GetEnum();
        }
        else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
        {
            Enum = ByteProperty->Enum;
        }
        if (!Enum || Exported.IsEmpty())
        {
            return FString();
        }

        FString ShortName = Exported;
        int32 Separator = INDEX_NONE;
        if (Exported.FindLastChar(TEXT(':'), Separator))
        {
            ShortName = Exported.Mid(Separator + 1);
        }
        ShortName.TrimStartAndEndInline();

        int64 Value = Enum->GetValueByNameString(ShortName);
        if (Value == INDEX_NONE)
        {
            Value = Enum->GetValueByNameString(Exported);
        }
        if (Value == INDEX_NONE)
        {
            return FString();
        }
        return Enum->GetDisplayNameTextByValue(Value).ToString();
    }

    TSharedPtr<FJsonObject> VariableToJson(UBlueprint* Blueprint, const FBPVariableDescription& Var)
    {
        TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
        if (Var.VarType.PinSubCategoryObject.IsValid())
        {
            VarObj->SetStringField(TEXT("sub_type"), Var.VarType.PinSubCategoryObject->GetName());
        }
        else if (!Var.VarType.PinSubCategory.IsNone())
        {
            VarObj->SetStringField(TEXT("sub_type"), Var.VarType.PinSubCategory.ToString());
        }
        AddContainerField(VarObj, Var.VarType);
        if (!Var.Category.IsEmpty())
        {
            VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
        }

        const bool bEditable = (Var.PropertyFlags & CPF_Edit) != 0
            && (Var.PropertyFlags & CPF_DisableEditOnInstance) == 0;
        VarObj->SetBoolField(TEXT("instance_editable"), bEditable);
        VarObj->SetBoolField(TEXT("blueprint_read_only"), (Var.PropertyFlags & CPF_BlueprintReadOnly) != 0);
        VarObj->SetBoolField(TEXT("replicated"), (Var.PropertyFlags & CPF_Net) != 0);
        if (!Var.RepNotifyFunc.IsNone())
        {
            VarObj->SetStringField(TEXT("rep_notify"), Var.RepNotifyFunc.ToString());
        }

        if (Blueprint->GeneratedClass)
        {
            if (UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject(false))
            {
                if (FProperty* Property = Blueprint->GeneratedClass->FindPropertyByName(Var.VarName))
                {
                    FString Exported;
                    Property->ExportText_InContainer(0, Exported, CDO, nullptr, CDO, PPF_None);
                    if (!Exported.IsEmpty())
                    {
                        VarObj->SetStringField(TEXT("default"), Exported.Left(400));
                    }
                    const FString Label = EnumLabelForExported(Property, Exported);
                    if (!Label.IsEmpty())
                    {
                        VarObj->SetStringField(TEXT("default_label"), Label);
                    }
                }
            }
        }

        return VarObj;
    }

    void AppendScsNodes(USCS_Node* Node, const FString& ParentName, bool bInherited, const FString& Source, TArray<TSharedPtr<FJsonValue>>& Out)
    {
        if (!Node)
        {
            return;
        }

        TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
        CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
        CompObj->SetStringField(TEXT("class"), Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT(""));
        if (!ParentName.IsEmpty())
        {
            CompObj->SetStringField(TEXT("parent"), ParentName);
        }
        CompObj->SetBoolField(TEXT("inherited"), bInherited);
        if (!Source.IsEmpty())
        {
            CompObj->SetStringField(TEXT("source"), Source);
        }
        Out.Add(MakeShared<FJsonValueObject>(CompObj));

        const FString ThisName = Node->GetVariableName().ToString();
        for (USCS_Node* Child : Node->GetChildNodes())
        {
            AppendScsNodes(Child, ThisName, bInherited, Source, Out);
        }
    }

    void AppendConstructionScript(USimpleConstructionScript* SCS, bool bInherited, const FString& Source, TArray<TSharedPtr<FJsonValue>>& Out)
    {
        if (!SCS)
        {
            return;
        }
        for (USCS_Node* Root : SCS->GetRootNodes())
        {
            AppendScsNodes(Root, FString(), bInherited, Source, Out);
        }
    }

    bool NodeMatchesEntry(const TSharedPtr<FJsonObject>& Node, const FString& Entry, bool bAllowContains)
    {
        const TCHAR* Fields[] = {
            TEXT("event"), TEXT("custom_event"), TEXT("function"), TEXT("variable"),
            TEXT("timeline"), TEXT("macro"), TEXT("collapsed_graph"), TEXT("component"), TEXT("delegate")
        };
        for (const TCHAR* Field : Fields)
        {
            const FString Value = JsonString(Node, Field);
            if (!Value.IsEmpty() && Value.Equals(Entry, ESearchCase::IgnoreCase))
            {
                return true;
            }
        }

        const FString Title = FirstTitleLine(JsonString(Node, TEXT("title")));
        if (Title.Equals(Entry, ESearchCase::IgnoreCase))
        {
            return true;
        }
        return bAllowContains && !Title.IsEmpty() && Title.Contains(Entry, ESearchCase::IgnoreCase);
    }

    FString PinKey(const FString& NodeId, const FString& PinName)
    {
        return NodeId + TEXT("|") + PinName;
    }

    struct FLinkEnd
    {
        FString NodeId;
        FString PinName;
    };

    TSharedPtr<FJsonObject> FilterGraphByEntry(const TSharedPtr<FJsonObject>& Graph, const FString& Entry, FString& OutError)
    {
        const TArray<TSharedPtr<FJsonValue>>* NodeValues = nullptr;
        if (!Graph->TryGetArrayField(TEXT("nodes"), NodeValues) || !NodeValues)
        {
            OutError = TEXT("El grafo no tiene nodos");
            return nullptr;
        }

        TMap<FString, TSharedPtr<FJsonObject>> NodesById;
        TArray<TSharedPtr<FJsonObject>> Nodes;
        for (const TSharedPtr<FJsonValue>& Value : *NodeValues)
        {
            TSharedPtr<FJsonObject> Node = Value->AsObject();
            if (!Node.IsValid())
            {
                continue;
            }
            Nodes.Add(Node);
            NodesById.Add(JsonString(Node, TEXT("id")), Node);
        }

        TArray<FString> EntryIds;
        for (const TSharedPtr<FJsonObject>& Node : Nodes)
        {
            if (NodeMatchesEntry(Node, Entry, false))
            {
                EntryIds.Add(JsonString(Node, TEXT("id")));
            }
        }
        if (EntryIds.Num() == 0)
        {
            for (const TSharedPtr<FJsonObject>& Node : Nodes)
            {
                if (NodeMatchesEntry(Node, Entry, true))
                {
                    EntryIds.Add(JsonString(Node, TEXT("id")));
                }
            }
        }
        if (EntryIds.Num() == 0)
        {
            return nullptr;
        }

        TMap<FString, TArray<FLinkEnd>> Outgoing;
        TMap<FString, TArray<FLinkEnd>> Incoming;
        const TArray<TSharedPtr<FJsonValue>>* LinkValues = nullptr;
        if (Graph->TryGetArrayField(TEXT("links"), LinkValues) && LinkValues)
        {
            for (const TSharedPtr<FJsonValue>& Value : *LinkValues)
            {
                const TSharedPtr<FJsonObject> Link = Value->AsObject();
                if (!Link.IsValid())
                {
                    continue;
                }
                const FString FromNode = JsonString(Link, TEXT("from_node"));
                const FString FromPin = JsonString(Link, TEXT("from_pin"));
                const FString ToNode = JsonString(Link, TEXT("to_node"));
                const FString ToPin = JsonString(Link, TEXT("to_pin"));
                Outgoing.FindOrAdd(PinKey(FromNode, FromPin)).Add({ToNode, ToPin});
                Incoming.FindOrAdd(PinKey(ToNode, ToPin)).Add({FromNode, FromPin});
            }
        }

        TSet<FString> Keep;
        TSet<FString> DataVisited;

        TFunction<void(const FString&)> KeepDataUpstream = [&](const FString& NodeId)
        {
            if (NodeId.IsEmpty() || DataVisited.Contains(NodeId))
            {
                return;
            }
            DataVisited.Add(NodeId);
            Keep.Add(NodeId);

            const TSharedPtr<FJsonObject>* Node = NodesById.Find(NodeId);
            if (!Node || !Node->IsValid())
            {
                return;
            }

            const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
            if (!(*Node)->TryGetArrayField(TEXT("pins"), Pins) || !Pins)
            {
                return;
            }

            for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
            {
                const TSharedPtr<FJsonObject> Pin = PinValue->AsObject();
                if (!Pin.IsValid() || JsonString(Pin, TEXT("direction")) != TEXT("in") || JsonString(Pin, TEXT("type")) == TEXT("exec"))
                {
                    continue;
                }
                if (const TArray<FLinkEnd>* Sources = Incoming.Find(PinKey(NodeId, JsonString(Pin, TEXT("name")))))
                {
                    for (const FLinkEnd& Source : *Sources)
                    {
                        KeepDataUpstream(Source.NodeId);
                    }
                }
            }
        };

        TSet<FString> ExecVisited;
        TFunction<void(const FString&)> WalkExec = [&](const FString& NodeId)
        {
            if (NodeId.IsEmpty() || ExecVisited.Contains(NodeId))
            {
                return;
            }
            ExecVisited.Add(NodeId);
            KeepDataUpstream(NodeId);

            const TSharedPtr<FJsonObject>* Node = NodesById.Find(NodeId);
            if (!Node || !Node->IsValid())
            {
                return;
            }

            const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
            if (!(*Node)->TryGetArrayField(TEXT("pins"), Pins) || !Pins)
            {
                return;
            }

            for (const TSharedPtr<FJsonValue>& PinValue : *Pins)
            {
                const TSharedPtr<FJsonObject> Pin = PinValue->AsObject();
                if (!Pin.IsValid() || JsonString(Pin, TEXT("direction")) != TEXT("out") || JsonString(Pin, TEXT("type")) != TEXT("exec"))
                {
                    continue;
                }
                if (const TArray<FLinkEnd>* Targets = Outgoing.Find(PinKey(NodeId, JsonString(Pin, TEXT("name")))))
                {
                    for (const FLinkEnd& Target : *Targets)
                    {
                        WalkExec(Target.NodeId);
                    }
                }
            }
        };

        for (const FString& EntryId : EntryIds)
        {
            WalkExec(EntryId);
        }

        TSharedPtr<FJsonObject> Filtered = MakeShared<FJsonObject>();
        Filtered->SetStringField(TEXT("name"), JsonString(Graph, TEXT("name")));
        Filtered->SetStringField(TEXT("schema"), JsonString(Graph, TEXT("schema")));
        if (Graph->HasField(TEXT("kind")))
        {
            Filtered->SetStringField(TEXT("kind"), JsonString(Graph, TEXT("kind")));
        }

        TArray<TSharedPtr<FJsonValue>> KeptNodes;
        for (const TSharedPtr<FJsonObject>& Node : Nodes)
        {
            if (Keep.Contains(JsonString(Node, TEXT("id"))))
            {
                KeptNodes.Add(MakeShared<FJsonValueObject>(Node));
            }
        }
        Filtered->SetArrayField(TEXT("nodes"), KeptNodes);

        TArray<TSharedPtr<FJsonValue>> KeptLinks;
        if (LinkValues)
        {
            for (const TSharedPtr<FJsonValue>& Value : *LinkValues)
            {
                const TSharedPtr<FJsonObject> Link = Value->AsObject();
                if (Link.IsValid()
                    && Keep.Contains(JsonString(Link, TEXT("from_node")))
                    && Keep.Contains(JsonString(Link, TEXT("to_node"))))
                {
                    KeptLinks.Add(MakeShared<FJsonValueObject>(Link));
                }
            }
        }
        Filtered->SetArrayField(TEXT("links"), KeptLinks);

        TArray<TSharedPtr<FJsonValue>> KeptComments;
        const TArray<TSharedPtr<FJsonValue>>* CommentValues = nullptr;
        if (Graph->TryGetArrayField(TEXT("comments"), CommentValues) && CommentValues)
        {
            for (const TSharedPtr<FJsonValue>& Value : *CommentValues)
            {
                const TSharedPtr<FJsonObject> Comment = Value->AsObject();
                if (!Comment.IsValid())
                {
                    continue;
                }

                TArray<TSharedPtr<FJsonValue>> KeptIds;
                const TArray<TSharedPtr<FJsonValue>>* Ids = nullptr;
                if (Comment->TryGetArrayField(TEXT("nodes"), Ids) && Ids)
                {
                    for (const TSharedPtr<FJsonValue>& IdValue : *Ids)
                    {
                        if (Keep.Contains(IdValue->AsString()))
                        {
                            KeptIds.Add(IdValue);
                        }
                    }
                }
                if (KeptIds.Num() == 0)
                {
                    continue;
                }

                TSharedPtr<FJsonObject> Copy = MakeShared<FJsonObject>();
                Copy->SetStringField(TEXT("id"), JsonString(Comment, TEXT("id")));
                Copy->SetStringField(TEXT("text"), JsonString(Comment, TEXT("text")));
                Copy->SetArrayField(TEXT("nodes"), KeptIds);
                KeptComments.Add(MakeShared<FJsonValueObject>(Copy));
            }
        }
        Filtered->SetArrayField(TEXT("comments"), KeptComments);
        return Filtered;
    }

    void PaginateGraph(const TSharedPtr<FJsonObject>& Graph, int32 Offset, int32 Limit)
    {
        const TArray<TSharedPtr<FJsonValue>>* NodeValues = nullptr;
        if (!Graph->TryGetArrayField(TEXT("nodes"), NodeValues) || !NodeValues)
        {
            return;
        }

        const int32 Total = NodeValues->Num();
        Offset = FMath::Clamp(Offset, 0, Total);
        const int32 End = Limit > 0 ? FMath::Min(Total, Offset + Limit) : Total;

        TSet<FString> PageIds;
        TArray<TSharedPtr<FJsonValue>> PageNodes;
        for (int32 Index = Offset; Index < End; ++Index)
        {
            PageNodes.Add((*NodeValues)[Index]);
            if (const TSharedPtr<FJsonObject> Node = (*NodeValues)[Index]->AsObject())
            {
                PageIds.Add(JsonString(Node, TEXT("id")));
            }
        }

        TArray<TSharedPtr<FJsonValue>> PageLinks;
        const TArray<TSharedPtr<FJsonValue>>* LinkValues = nullptr;
        if (Graph->TryGetArrayField(TEXT("links"), LinkValues) && LinkValues)
        {
            for (const TSharedPtr<FJsonValue>& Value : *LinkValues)
            {
                const TSharedPtr<FJsonObject> Link = Value->AsObject();
                if (!Link.IsValid())
                {
                    continue;
                }
                if (PageIds.Contains(JsonString(Link, TEXT("from_node"))) || PageIds.Contains(JsonString(Link, TEXT("to_node"))))
                {
                    PageLinks.Add(Value);
                }
            }
        }

        Graph->SetArrayField(TEXT("nodes"), PageNodes);
        Graph->SetArrayField(TEXT("links"), PageLinks);
        Graph->SetNumberField(TEXT("node_total"), Total);
        Graph->SetNumberField(TEXT("offset"), Offset);
        Graph->SetNumberField(TEXT("limit"), Limit);
    }

    FString AvailableEntries(const TSharedPtr<FJsonObject>& Graph)
    {
        TArray<FString> Names;
        Names.Add(JsonString(Graph, TEXT("name")));
        const TArray<TSharedPtr<FJsonValue>>* NodeValues = nullptr;
        if (Graph->TryGetArrayField(TEXT("nodes"), NodeValues) && NodeValues)
        {
            for (const TSharedPtr<FJsonValue>& Value : *NodeValues)
            {
                const TSharedPtr<FJsonObject> Node = Value->AsObject();
                if (!Node.IsValid())
                {
                    continue;
                }
                for (const TCHAR* Field : {TEXT("event"), TEXT("custom_event")})
                {
                    const FString Name = JsonString(Node, Field);
                    if (!Name.IsEmpty())
                    {
                        Names.AddUnique(Name);
                    }
                }
            }
        }
        return FString::Join(Names, TEXT(", "));
    }

    bool NodeMatchesQuery(UEdGraphNode* Node, const FString& Query, FString& OutHow)
    {
        if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
        {
            if (Call->FunctionReference.GetMemberName().ToString().Contains(Query, ESearchCase::IgnoreCase))
            {
                OutHow = TEXT("function");
                return true;
            }
        }
        if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
        {
            if (Variable->GetVarNameString().Contains(Query, ESearchCase::IgnoreCase))
            {
                OutHow = TEXT("variable");
                return true;
            }
        }
        if (UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
        {
            if (CustomEvent->CustomFunctionName.ToString().Contains(Query, ESearchCase::IgnoreCase))
            {
                OutHow = TEXT("event");
                return true;
            }
        }
        else if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
        {
            if (Event->EventReference.GetMemberName().ToString().Contains(Query, ESearchCase::IgnoreCase))
            {
                OutHow = TEXT("event");
                return true;
            }
        }

        const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
        if (Title.Contains(Query, ESearchCase::IgnoreCase))
        {
            OutHow = TEXT("title");
            return true;
        }
        return false;
    }

    void SearchGraph(UBlueprint* Blueprint, UEdGraph* Graph, const FString& Query, int32 MaxResults, TArray<TSharedPtr<FJsonValue>>& OutHits, TSet<UEdGraph*>& Seen)
    {
        if (!Graph || Seen.Contains(Graph) || OutHits.Num() >= MaxResults)
        {
            return;
        }
        Seen.Add(Graph);

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }

            FString How;
            if (NodeMatchesQuery(Node, Query, How))
            {
                TSharedPtr<FJsonObject> Hit = MakeShared<FJsonObject>();
                Hit->SetStringField(TEXT("blueprint"), Blueprint->GetName());
                Hit->SetStringField(TEXT("path"), Blueprint->GetPathName());
                Hit->SetStringField(TEXT("graph"), Graph->GetName());
                Hit->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString(EGuidFormats::Digits));
                Hit->SetStringField(TEXT("class"), Node->GetClass()->GetName());
                Hit->SetStringField(TEXT("title"), FirstTitleLine(Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString()).Left(200));
                Hit->SetStringField(TEXT("matched_on"), How);
                OutHits.Add(MakeShared<FJsonValueObject>(Hit));
                if (OutHits.Num() >= MaxResults)
                {
                    return;
                }
            }

            if (UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node))
            {
                SearchGraph(Blueprint, Composite->BoundGraph, Query, MaxResults, OutHits, Seen);
            }
        }
    }

    TSharedPtr<FJsonObject> ReadDataTable(UDataTable* Table, int32 Offset, int32 Limit)
    {
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("kind"), TEXT("datatable"));
        Data->SetStringField(TEXT("name"), Table->GetName());
        Data->SetStringField(TEXT("path"), Table->GetPathName());
        Data->SetStringField(TEXT("row_struct"), Table->GetRowStruct() ? Table->GetRowStruct()->GetName() : TEXT(""));

        TArray<TSharedPtr<FJsonValue>> Fields;
        if (const UScriptStruct* RowStruct = Table->GetRowStruct())
        {
            for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
            {
                TSharedPtr<FJsonObject> Field = MakeShared<FJsonObject>();
                Field->SetStringField(TEXT("name"), It->GetAuthoredName());
                FString Extended;
                Field->SetStringField(TEXT("type"), It->GetCPPType(&Extended) + Extended);
                Fields.Add(MakeShared<FJsonValueObject>(Field));
            }
        }
        Data->SetArrayField(TEXT("fields"), Fields);

        const TArray<FName> RowNames = Table->GetRowNames();
        const int32 Total = RowNames.Num();
        Offset = FMath::Clamp(Offset, 0, Total);
        const int32 End = Limit > 0 ? FMath::Min(Total, Offset + Limit) : Total;

        TArray<TSharedPtr<FJsonValue>> Rows;
        for (int32 Index = Offset; Index < End; ++Index)
        {
            uint8* RowData = Table->FindRowUnchecked(RowNames[Index]);
            TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("name"), RowNames[Index].ToString());
            if (RowData && Table->GetRowStruct())
            {
                TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
                FJsonObjectConverter::UStructToJsonObject(Table->GetRowStruct(), RowData, Values, 0, CPF_Transient);
                Row->SetObjectField(TEXT("values"), Values);
            }
            Rows.Add(MakeShared<FJsonValueObject>(Row));
        }

        Data->SetNumberField(TEXT("total_rows"), Total);
        Data->SetNumberField(TEXT("offset"), Offset);
        Data->SetNumberField(TEXT("limit"), Limit);
        Data->SetArrayField(TEXT("rows"), Rows);
        return Data;
    }

    TSharedPtr<FJsonObject> ReadUserStruct(UUserDefinedStruct* Struct)
    {
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("kind"), TEXT("struct"));
        Data->SetStringField(TEXT("name"), Struct->GetName());
        Data->SetStringField(TEXT("path"), Struct->GetPathName());

        TArray<TSharedPtr<FJsonValue>> Fields;
        for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            FProperty* Property = *It;
            if (Property->HasAnyPropertyFlags(CPF_Transient))
            {
                continue;
            }

            TSharedPtr<FJsonObject> Field = MakeShared<FJsonObject>();
            Field->SetStringField(TEXT("name"), Struct->GetAuthoredNameForField(Property));
            FString Extended;
            Field->SetStringField(TEXT("type"), Property->GetCPPType(&Extended) + Extended);
            if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
            {
                if (EnumProperty->GetEnum())
                {
                    Field->SetStringField(TEXT("enum"), EnumProperty->GetEnum()->GetName());
                }
            }
            Fields.Add(MakeShared<FJsonValueObject>(Field));
        }
        Data->SetArrayField(TEXT("fields"), Fields);
        return Data;
    }

    TSharedPtr<FJsonObject> ReadEnum(UEnum* Enum)
    {
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("kind"), TEXT("enum"));
        Data->SetStringField(TEXT("name"), Enum->GetName());
        Data->SetStringField(TEXT("path"), Enum->GetPathName());

        TArray<TSharedPtr<FJsonValue>> Values;
        for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
        {
            const FString Name = Enum->GetNameStringByIndex(Index);
            if (Name.EndsWith(TEXT("_MAX")))
            {
                continue;
            }

            TSharedPtr<FJsonObject> Value = MakeShared<FJsonObject>();
            Value->SetStringField(TEXT("name"), Name);
            Value->SetStringField(TEXT("label"), Enum->GetDisplayNameTextByIndex(Index).ToString());
            Value->SetNumberField(TEXT("value"), static_cast<double>(Enum->GetValueByIndex(Index)));
            Values.Add(MakeShared<FJsonValueObject>(Value));
        }
        Data->SetArrayField(TEXT("values"), Values);
        return Data;
    }
}

FUnrealMCPBlueprintReadCommands::FUnrealMCPBlueprintReadCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintReadCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("list_blueprints"))
    {
        return HandleListBlueprints(Params);
    }
    if (CommandType == TEXT("get_blueprint_overview"))
    {
        return HandleGetBlueprintOverview(Params);
    }
    if (CommandType == TEXT("read_blueprint_graph"))
    {
        return HandleReadBlueprintGraph(Params);
    }
    if (CommandType == TEXT("find_in_blueprints"))
    {
        return HandleFindInBlueprints(Params);
    }
    if (CommandType == TEXT("export_blueprint_nodes_text"))
    {
        return HandleExportBlueprintNodesText(Params);
    }
    if (CommandType == TEXT("read_data_asset"))
    {
        return HandleReadDataAsset(Params);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown blueprint read command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintReadCommands::HandleListBlueprints(const TSharedPtr<FJsonObject>& Params)
{
    FString Path = TEXT("/Game");
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("path"), Path);
    }
    Path.TrimStartAndEndInline();
    if (Path.IsEmpty())
    {
        Path = TEXT("/Game");
    }

    FString Kind;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("kind"), Kind);
    }
    Kind.TrimStartAndEndInline();
    const int32 Limit = FMath::Max(1, ReadIntParam(Params, TEXT("limit"), 300));

    auto Wants = [&Kind](const TCHAR* Name)
    {
        return Kind.IsEmpty() || Kind.Equals(Name, ESearchCase::IgnoreCase);
    };

    FARFilter Filter;
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;
    Filter.PackagePaths.Add(FName(*Path));
    if (Wants(TEXT("blueprint")))
    {
        Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    }
    if (Wants(TEXT("map")))
    {
        Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
    }
    if (Wants(TEXT("datatable")))
    {
        Filter.ClassPaths.Add(UDataTable::StaticClass()->GetClassPathName());
    }
    if (Wants(TEXT("struct")))
    {
        Filter.ClassPaths.Add(UUserDefinedStruct::StaticClass()->GetClassPathName());
    }
    if (Wants(TEXT("enum")))
    {
        Filter.ClassPaths.Add(UUserDefinedEnum::StaticClass()->GetClassPathName());
    }
    if (Wants(TEXT("input")))
    {
        Filter.ClassPaths.Add(UInputAction::StaticClass()->GetClassPathName());
        Filter.ClassPaths.Add(UInputMappingContext::StaticClass()->GetClassPathName());
    }
    if (Filter.ClassPaths.Num() == 0)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("kind tiene que ser blueprint, map, datatable, struct, enum o input"));
    }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Assets;
    AssetRegistryModule.Get().GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> Items;
    for (const FAssetData& Asset : Assets)
    {
        if (Asset.IsRedirector())
        {
            continue;
        }
        if (Items.Num() >= Limit)
        {
            break;
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Asset.AssetName.ToString());
        Item->SetStringField(TEXT("path"), Asset.GetSoftObjectPath().ToString());
        Item->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());

        FString Parent;
        if (!Asset.GetTagValue(FName(TEXT("ParentClass")), Parent))
        {
            Asset.GetTagValue(FName(TEXT("NativeParentClass")), Parent);
        }
        if (!Parent.IsEmpty())
        {
            Item->SetStringField(TEXT("parent"), ShortRegistryTag(Parent));
        }
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("path"), Path);
    Data->SetNumberField(TEXT("count"), Items.Num());
    Data->SetNumberField(TEXT("matched"), Assets.Num());
    Data->SetArrayField(TEXT("assets"), Items);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintReadCommands::HandleGetBlueprintOverview(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'blueprint_path'"));
    }

    FString ResolveError;
    UBlueprint* Blueprint = UnrealMCPGraph::ResolveBlueprint(UnrealMCPGraph::NormalizeObjectPath(BlueprintPath), ResolveError);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ResolveError);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("blueprint"), Blueprint->GetName());
    Data->SetStringField(TEXT("path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("parent"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT(""));
    if (Blueprint->ParentClass)
    {
        Data->SetStringField(TEXT("parent_path"), Blueprint->ParentClass->GetPathName());
    }
    Data->SetStringField(TEXT("status"), StatusToString(Blueprint->Status));

    TArray<TSharedPtr<FJsonValue>> Interfaces;
    for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
    {
        TSharedPtr<FJsonObject> InterfaceObj = MakeShared<FJsonObject>();
        InterfaceObj->SetStringField(TEXT("name"), InterfaceDesc.Interface ? InterfaceDesc.Interface->GetName() : TEXT(""));
        TArray<TSharedPtr<FJsonValue>> GraphNames;
        for (UEdGraph* Graph : InterfaceDesc.Graphs)
        {
            if (Graph)
            {
                GraphNames.Add(MakeShared<FJsonValueString>(Graph->GetName()));
            }
        }
        InterfaceObj->SetArrayField(TEXT("graphs"), GraphNames);
        Interfaces.Add(MakeShared<FJsonValueObject>(InterfaceObj));
    }
    Data->SetArrayField(TEXT("interfaces"), Interfaces);

    TArray<TSharedPtr<FJsonValue>> Components;
    AppendConstructionScript(Blueprint->SimpleConstructionScript, false, Blueprint->GetName(), Components);
    UClass* ParentClass = Blueprint->ParentClass;
    while (UBlueprintGeneratedClass* Generated = Cast<UBlueprintGeneratedClass>(ParentClass))
    {
        if (UBlueprint* ParentBlueprint = Cast<UBlueprint>(Generated->ClassGeneratedBy))
        {
            AppendConstructionScript(ParentBlueprint->SimpleConstructionScript, true, ParentBlueprint->GetName(), Components);
        }
        ParentClass = ParentClass->GetSuperClass();
    }
    Data->SetArrayField(TEXT("components"), Components);

    TArray<TSharedPtr<FJsonValue>> Variables;
    for (const FBPVariableDescription& Var : Blueprint->NewVariables)
    {
        Variables.Add(MakeShared<FJsonValueObject>(VariableToJson(Blueprint, Var)));
    }
    Data->SetArrayField(TEXT("variables"), Variables);

    TArray<TSharedPtr<FJsonValue>> Functions;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (!Graph)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Function = MakeShared<FJsonObject>();
        Function->SetStringField(TEXT("name"), Graph->GetName());
        TArray<TSharedPtr<FJsonValue>> Outputs;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
            {
                Function->SetArrayField(TEXT("inputs"), DataPinsJson(Entry, EGPD_Output));
            }
            if (UK2Node_FunctionResult* Result = Cast<UK2Node_FunctionResult>(Node))
            {
                Outputs.Append(DataPinsJson(Result, EGPD_Input));
            }
        }
        Function->SetArrayField(TEXT("outputs"), Outputs);
        Functions.Add(MakeShared<FJsonValueObject>(Function));
    }
    Data->SetArrayField(TEXT("functions"), Functions);

    TArray<TSharedPtr<FJsonValue>> Events;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (!Graph)
        {
            continue;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
            {
                TSharedPtr<FJsonObject> Event = MakeShared<FJsonObject>();
                Event->SetStringField(TEXT("name"), CustomEvent->CustomFunctionName.ToString());
                Event->SetStringField(TEXT("kind"), TEXT("custom"));
                Event->SetStringField(TEXT("graph"), Graph->GetName());
                Event->SetArrayField(TEXT("parameters"), DataPinsJson(CustomEvent, EGPD_Output));
                Events.Add(MakeShared<FJsonValueObject>(Event));
            }
            else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
            {
                TSharedPtr<FJsonObject> Event = MakeShared<FJsonObject>();
                Event->SetStringField(TEXT("name"), EventNode->EventReference.GetMemberName().ToString());
                Event->SetStringField(TEXT("kind"), TEXT("override"));
                Event->SetStringField(TEXT("graph"), Graph->GetName());
                Events.Add(MakeShared<FJsonValueObject>(Event));
            }
        }
    }
    Data->SetArrayField(TEXT("events"), Events);

    TArray<TSharedPtr<FJsonValue>> Dispatchers;
    for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
    {
        if (!Graph)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Dispatcher = MakeShared<FJsonObject>();
        Dispatcher->SetStringField(TEXT("name"), Graph->GetName());
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
            {
                Dispatcher->SetArrayField(TEXT("parameters"), DataPinsJson(Entry, EGPD_Output));
                break;
            }
        }
        Dispatchers.Add(MakeShared<FJsonValueObject>(Dispatcher));
    }
    Data->SetArrayField(TEXT("dispatchers"), Dispatchers);

    TArray<TSharedPtr<FJsonValue>> Macros;
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (!Graph)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Macro = MakeShared<FJsonObject>();
        Macro->SetStringField(TEXT("name"), Graph->GetName());
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Node);
            if (!Tunnel)
            {
                continue;
            }
            if (Tunnel->DrawNodeAsEntry())
            {
                Macro->SetArrayField(TEXT("inputs"), DataPinsJson(Tunnel, EGPD_Output));
            }
            else if (Tunnel->DrawNodeAsExit())
            {
                Macro->SetArrayField(TEXT("outputs"), DataPinsJson(Tunnel, EGPD_Input));
            }
        }
        Macros.Add(MakeShared<FJsonValueObject>(Macro));
    }
    Data->SetArrayField(TEXT("macros"), Macros);

    TArray<UEdGraph*> Graphs;
    UnrealMCPGraph::CollectBlueprintGraphs(Blueprint, Graphs);
    TArray<TSharedPtr<FJsonValue>> GraphSummaries;
    TArray<TSharedPtr<FJsonValue>> Messages;
    TSet<UEdGraph*> Seen;
    for (UEdGraph* Graph : Graphs)
    {
        if (!Graph)
        {
            continue;
        }

        TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
        Summary->SetStringField(TEXT("name"), Graph->GetName());
        Summary->SetStringField(TEXT("kind"), GraphKind(Blueprint, Graph));
        Summary->SetNumberField(TEXT("nodes"), Graph->Nodes.Num());
        GraphSummaries.Add(MakeShared<FJsonValueObject>(Summary));

        TArray<UEdGraph*> Collapsed;
        CollectCollapsedGraphs(Graph, Collapsed, Seen);
        for (UEdGraph* CollapsedGraph : Collapsed)
        {
            TSharedPtr<FJsonObject> CollapsedSummary = MakeShared<FJsonObject>();
            CollapsedSummary->SetStringField(TEXT("name"), CollapsedGraph->GetName());
            CollapsedSummary->SetStringField(TEXT("kind"), TEXT("collapsed"));
            CollapsedSummary->SetStringField(TEXT("parent"), Graph->GetName());
            CollapsedSummary->SetNumberField(TEXT("nodes"), CollapsedGraph->Nodes.Num());
            GraphSummaries.Add(MakeShared<FJsonValueObject>(CollapsedSummary));
        }
    }

    auto CollectMessages = [&Messages](UEdGraph* Graph, const FString& GraphName)
    {
        if (!Graph || Messages.Num() >= 40)
        {
            return;
        }
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node || !Node->bHasCompilerMessage || Node->ErrorMsg.IsEmpty())
            {
                continue;
            }
            TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
            Message->SetStringField(TEXT("graph"), GraphName);
            Message->SetStringField(TEXT("node"), FirstTitleLine(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()).Left(160));
            Message->SetStringField(TEXT("severity"), SeverityToString(Node->ErrorType));
            Message->SetStringField(TEXT("message"), Node->ErrorMsg.Left(300));
            Messages.Add(MakeShared<FJsonValueObject>(Message));
            if (Messages.Num() >= 40)
            {
                return;
            }
        }
    };

    Seen.Reset();
    for (UEdGraph* Graph : Graphs)
    {
        CollectMessages(Graph, Graph ? Graph->GetName() : FString());
        TArray<UEdGraph*> Collapsed;
        CollectCollapsedGraphs(Graph, Collapsed, Seen);
        for (UEdGraph* CollapsedGraph : Collapsed)
        {
            CollectMessages(CollapsedGraph, CollapsedGraph->GetName());
        }
    }

    Data->SetArrayField(TEXT("graphs"), GraphSummaries);
    Data->SetArrayField(TEXT("compile_messages"), Messages);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintReadCommands::HandleReadBlueprintGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'blueprint_path'"));
    }

    FString GraphName;
    FString Entry;
    Params->TryGetStringField(TEXT("graph_name"), GraphName);
    Params->TryGetStringField(TEXT("entry"), Entry);
    GraphName.TrimStartAndEndInline();
    Entry.TrimStartAndEndInline();
    const int32 Offset = FMath::Max(0, ReadIntParam(Params, TEXT("offset"), 0));
    const int32 Limit = FMath::Max(0, ReadIntParam(Params, TEXT("limit"), 0));

    FString ResolveError;
    UBlueprint* Blueprint = UnrealMCPGraph::ResolveBlueprint(UnrealMCPGraph::NormalizeObjectPath(BlueprintPath), ResolveError);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ResolveError);
    }

    TArray<UEdGraph*> AllGraphs;
    UnrealMCPGraph::CollectBlueprintGraphs(Blueprint, AllGraphs);

    TArray<TPair<UEdGraph*, FString>> Selected;
    bool bFilterNodes = !Entry.IsEmpty();
    if (!GraphName.IsEmpty())
    {
        bool bCollapsed = false;
        UEdGraph* Graph = FindGraphByName(Blueprint, GraphName, bCollapsed);
        if (!Graph)
        {
            TArray<FString> Names;
            for (UEdGraph* Candidate : AllGraphs)
            {
                if (Candidate)
                {
                    Names.Add(Candidate->GetName());
                }
            }
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No está el grafo '%s'. Grafos: %s"), *GraphName, *FString::Join(Names, TEXT(", "))));
        }
        Selected.Add(TPair<UEdGraph*, FString>(Graph, bCollapsed ? TEXT("collapsed") : GraphKind(Blueprint, Graph)));
        if (Graph->GetName().Equals(Entry, ESearchCase::IgnoreCase))
        {
            bFilterNodes = false;
        }
    }
    else if (!Entry.IsEmpty())
    {
        for (UEdGraph* Graph : AllGraphs)
        {
            if (Graph && Graph->GetName().Equals(Entry, ESearchCase::IgnoreCase))
            {
                Selected.Add(TPair<UEdGraph*, FString>(Graph, GraphKind(Blueprint, Graph)));
                bFilterNodes = false;
                break;
            }
        }
        if (Selected.Num() == 0)
        {
            for (UEdGraph* Graph : AllGraphs)
            {
                if (Graph)
                {
                    Selected.Add(TPair<UEdGraph*, FString>(Graph, GraphKind(Blueprint, Graph)));
                }
            }
        }
    }
    else
    {
        for (UEdGraph* Graph : AllGraphs)
        {
            if (Graph)
            {
                Selected.Add(TPair<UEdGraph*, FString>(Graph, GraphKind(Blueprint, Graph)));
            }
        }
        bFilterNodes = false;
    }

    TArray<TSharedPtr<FJsonValue>> GraphValues;
    for (const TPair<UEdGraph*, FString>& Item : Selected)
    {
        TSharedPtr<FJsonObject> GraphObj = UnrealMCPGraph::GraphToJson(Item.Key);
        if (!GraphObj.IsValid())
        {
            continue;
        }
        GraphObj->SetStringField(TEXT("kind"), Item.Value);

        if (bFilterNodes)
        {
            FString FilterError;
            TSharedPtr<FJsonObject> Filtered = FilterGraphByEntry(GraphObj, Entry, FilterError);
            if (!Filtered.IsValid())
            {
                continue;
            }
            GraphObj = Filtered;
        }

        if ((Offset > 0 || Limit > 0) && Selected.Num() == 1)
        {
            PaginateGraph(GraphObj, Offset, Limit);
        }
        GraphValues.Add(MakeShared<FJsonValueObject>(GraphObj));
    }

    if (GraphValues.Num() == 0)
    {
        TArray<FString> Names;
        for (UEdGraph* Graph : AllGraphs)
        {
            if (!Graph)
            {
                continue;
            }
            TSharedPtr<FJsonObject> GraphObj = UnrealMCPGraph::GraphToJson(Graph);
            Names.Add(AvailableEntries(GraphObj));
        }
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("No hay una entrada '%s'. Entradas: %s"), *Entry, *FString::Join(Names, TEXT(" | "))));
    }

    TArray<TSharedPtr<FJsonValue>> Variables;
    for (const FBPVariableDescription& Var : Blueprint->NewVariables)
    {
        TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
        if (Var.VarType.PinSubCategoryObject.IsValid())
        {
            VarObj->SetStringField(TEXT("sub_type"), Var.VarType.PinSubCategoryObject->GetName());
        }
        AddContainerField(VarObj, Var.VarType);
        const TSharedPtr<FJsonObject> Rich = VariableToJson(Blueprint, Var);
        FString Label;
        if (Rich->TryGetStringField(TEXT("default_label"), Label))
        {
            VarObj->SetStringField(TEXT("default_label"), Label);
        }
        Variables.Add(MakeShared<FJsonValueObject>(VarObj));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("blueprint"), Blueprint->GetName());
    Data->SetStringField(TEXT("path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("parent"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT(""));
    Data->SetArrayField(TEXT("variables"), Variables);
    Data->SetArrayField(TEXT("graphs"), GraphValues);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintReadCommands::HandleFindInBlueprints(const TSharedPtr<FJsonObject>& Params)
{
    FString Query;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("query"), Query) || Query.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'query'"));
    }
    Query.TrimStartAndEndInline();

    FString Path = TEXT("/Game");
    Params->TryGetStringField(TEXT("path"), Path);
    if (Path.IsEmpty())
    {
        Path = TEXT("/Game");
    }
    const int32 MaxAssets = FMath::Clamp(ReadIntParam(Params, TEXT("max_assets"), 200), 1, 500);
    const int32 MaxResults = FMath::Clamp(ReadIntParam(Params, TEXT("max_results"), 80), 1, 200);

    FARFilter Filter;
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;
    Filter.PackagePaths.Add(FName(*Path));
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> Assets;
    AssetRegistryModule.Get().GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> Hits;
    int32 Scanned = 0;
    for (const FAssetData& Asset : Assets)
    {
        if (Scanned >= MaxAssets || Hits.Num() >= MaxResults)
        {
            break;
        }
        if (Asset.IsRedirector() || Asset.AssetClassPath.GetAssetName() == TEXT("World"))
        {
            continue;
        }

        UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
        if (!Blueprint)
        {
            continue;
        }
        ++Scanned;

        TArray<UEdGraph*> Graphs;
        UnrealMCPGraph::CollectBlueprintGraphs(Blueprint, Graphs);
        TSet<UEdGraph*> Seen;
        for (UEdGraph* Graph : Graphs)
        {
            SearchGraph(Blueprint, Graph, Query, MaxResults, Hits, Seen);
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("query"), Query);
    Data->SetStringField(TEXT("path"), Path);
    Data->SetNumberField(TEXT("scanned"), Scanned);
    Data->SetNumberField(TEXT("count"), Hits.Num());
    Data->SetBoolField(TEXT("truncated"), Scanned >= MaxAssets || Hits.Num() >= MaxResults);
    Data->SetArrayField(TEXT("hits"), Hits);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintReadCommands::HandleExportBlueprintNodesText(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    FString GraphName;
    if (!Params.IsValid()
        || !Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty()
        || !Params->TryGetStringField(TEXT("graph_name"), GraphName) || GraphName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Faltan 'blueprint_path' y 'graph_name'"));
    }

    TSet<FString> RequestedIds;
    const TArray<TSharedPtr<FJsonValue>>* IdValues = nullptr;
    if (Params->TryGetArrayField(TEXT("node_ids"), IdValues) && IdValues)
    {
        for (const TSharedPtr<FJsonValue>& Value : *IdValues)
        {
            FString Id = Value->AsString();
            Id.ReplaceInline(TEXT("-"), TEXT(""));
            RequestedIds.Add(Id.ToLower());
        }
    }

    FString ResolveError;
    UBlueprint* Blueprint = UnrealMCPGraph::ResolveBlueprint(UnrealMCPGraph::NormalizeObjectPath(BlueprintPath), ResolveError);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ResolveError);
    }

    bool bCollapsed = false;
    UEdGraph* Graph = FindGraphByName(Blueprint, GraphName, bCollapsed);
    if (!Graph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No está el grafo '%s'"), *GraphName));
    }

    TSet<UObject*> NodesToExport;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            continue;
        }
        const FString Id = Node->NodeGuid.ToString(EGuidFormats::Digits).ToLower();
        if (RequestedIds.Num() == 0 || RequestedIds.Contains(Id))
        {
            NodesToExport.Add(Node);
        }
    }
    if (NodesToExport.Num() == 0)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Ningún nodo coincide con node_ids"));
    }

    FString Exported;
    FEdGraphUtilities::ExportNodesToText(NodesToExport, Exported);
    if (Exported.Len() > 500000)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("El texto exportado supera 500000 caracteres. Pasa node_ids para un subconjunto."));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("blueprint"), Blueprint->GetName());
    Data->SetStringField(TEXT("graph"), Graph->GetName());
    Data->SetNumberField(TEXT("nodes"), NodesToExport.Num());
    Data->SetStringField(TEXT("text"), Exported);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintReadCommands::HandleReadDataAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'asset_path'"));
    }

    const int32 Offset = FMath::Max(0, ReadIntParam(Params, TEXT("offset"), 0));
    const int32 Limit = FMath::Max(0, ReadIntParam(Params, TEXT("limit"), 50));
    const FString ObjectPath = UnrealMCPGraph::NormalizeObjectPath(AssetPath);
    UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPath);
    if (!Asset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No se encontró el asset: %s"), *ObjectPath));
    }

    TSharedPtr<FJsonObject> Data;
    if (UDataTable* Table = Cast<UDataTable>(Asset))
    {
        Data = ReadDataTable(Table, Offset, Limit);
    }
    else if (UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(Asset))
    {
        Data = ReadUserStruct(Struct);
    }
    else if (UEnum* Enum = Cast<UEnum>(Asset))
    {
        Data = ReadEnum(Enum);
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("%s no es DataTable, Struct ni Enum (%s)"), *Asset->GetName(), *Asset->GetClass()->GetName()));
    }

    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}
