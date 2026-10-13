#include "Commands/UnrealMCPGraphEditCommands.h"

#include "Commands/UnrealMCPCommonUtils.h"
#include "Commands/UnrealMCPGraphSerializer.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_Self.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_EnhancedInputAction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "InputAction.h"

namespace
{
    FString NodeId(const UEdGraphNode* Node)
    {
        return Node ? Node->NodeGuid.ToString(EGuidFormats::Digits) : FString();
    }

    float ReadNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, float DefaultValue)
    {
        double Value = DefaultValue;
        if (Object.IsValid())
        {
            Object->TryGetNumberField(Name, Value);
        }
        return static_cast<float>(Value);
    }

    FString ReadString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
    {
        FString Value;
        if (Object.IsValid())
        {
            Object->TryGetStringField(Name, Value);
        }
        Value.TrimStartAndEndInline();
        return Value;
    }

    void ReadPinSpec(const TSharedPtr<FJsonObject>& Object, FString& Type, FString& SubType, FString& Container)
    {
        Type = ReadString(Object, TEXT("type"));
        if (Type.IsEmpty())
        {
            Type = ReadString(Object, TEXT("variable_type"));
        }
        SubType = ReadString(Object, TEXT("sub_type"));
        if (SubType.IsEmpty())
        {
            SubType = ReadString(Object, TEXT("object_class"));
        }
        Container = ReadString(Object, TEXT("container"));
    }

    bool AddUserPins(UK2Node_EditablePinBase* Node, const TArray<TSharedPtr<FJsonValue>>* Pins, EEdGraphPinDirection Direction, FString& OutError)
    {
        if (!Node || !Pins)
        {
            return true;
        }
        for (const TSharedPtr<FJsonValue>& Value : *Pins)
        {
            const TSharedPtr<FJsonObject> Pin = Value->AsObject();
            const FString Name = ReadString(Pin, TEXT("name"));
            if (Name.IsEmpty())
            {
                OutError = TEXT("Un pin no tiene nombre");
                return false;
            }
            FString Type;
            FString SubType;
            FString Container;
            ReadPinSpec(Pin, Type, SubType, Container);
            FEdGraphPinType PinType;
            if (!FUnrealMCPCommonUtils::BuildPinType(Type, SubType, Container, PinType, OutError))
            {
                return false;
            }
            Node->CreateUserDefinedPin(FName(*Name), PinType, Direction);
        }
        return true;
    }

    UEdGraphNode* FindNode(UEdGraph* Graph, const TMap<FString, UEdGraphNode*>& Refs, const FString& RefOrId)
    {
        if (UEdGraphNode* const* Created = Refs.Find(RefOrId))
        {
            return *Created;
        }
        const FString Normalized = RefOrId.Replace(TEXT("-"), TEXT("")).ToLower();
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node && Node->NodeGuid.ToString(EGuidFormats::Digits).ToLower() == Normalized)
            {
                return Node;
            }
        }
        return nullptr;
    }

    bool SplitEndpoint(const FString& Text, FString& OutNode, FString& OutPin)
    {
        int32 Dot = INDEX_NONE;
        if (!Text.FindLastChar(TEXT('.'), Dot) || Dot <= 0)
        {
            return false;
        }
        OutNode = Text.Left(Dot);
        OutPin = Text.Mid(Dot + 1);
        return !OutNode.IsEmpty() && !OutPin.IsEmpty();
    }

    UEdGraphPin* FindPinEither(UEdGraphNode* Node, const FString& PinName)
    {
        if (UEdGraphPin* Pin = FUnrealMCPCommonUtils::FindPin(Node, PinName, EGPD_Output))
        {
            return Pin;
        }
        return FUnrealMCPCommonUtils::FindPin(Node, PinName, EGPD_Input);
    }

    void ApplyDefaults(UEdGraph* Graph, UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Spec)
    {
        const TSharedPtr<FJsonObject>* Defaults = nullptr;
        if (!Spec.IsValid() || !Spec->TryGetObjectField(TEXT("defaults"), Defaults) || !Defaults || !Graph->GetSchema())
        {
            return;
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Defaults)->Values)
        {
            UEdGraphPin* Pin = FindPinEither(Node, Pair.Key);
            if (!Pin)
            {
                continue;
            }
            FString Text;
            if (Pair.Value->Type == EJson::Boolean)
            {
                Text = Pair.Value->AsBool() ? TEXT("true") : TEXT("false");
            }
            else if (Pair.Value->Type == EJson::Number)
            {
                Text = FString::SanitizeFloat(Pair.Value->AsNumber());
            }
            else
            {
                Text = Pair.Value->AsString();
            }
            Graph->GetSchema()->TrySetDefaultValue(*Pin, Text);
        }
    }

    TArray<TSharedPtr<FJsonValue>> CompileBlueprint(UBlueprint* Blueprint)
    {
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
        TArray<TSharedPtr<FJsonValue>> Messages;
        for (const TSharedRef<FTokenizedMessage>& Message : Log.Messages)
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            FString Severity = TEXT("info");
            if (Message->GetSeverity() <= EMessageSeverity::Error)
            {
                Severity = TEXT("error");
            }
            else if (Message->GetSeverity() <= EMessageSeverity::Warning)
            {
                Severity = TEXT("warning");
            }
            Item->SetStringField(TEXT("severity"), Severity);
            Item->SetStringField(TEXT("message"), Message->ToText().ToString().Left(400));
            Messages.Add(MakeShared<FJsonValueObject>(Item));
        }
        return Messages;
    }

    void AddFunctionHits(UClass* Class, const FString& Query, TArray<TSharedPtr<FJsonValue>>& Out)
    {
        if (!Class)
        {
            return;
        }
        for (TFieldIterator<UFunction> It(Class); It && Out.Num() < 40; ++It)
        {
            UFunction* Function = *It;
            if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable) || Function->HasAnyFunctionFlags(FUNC_Delegate))
            {
                continue;
            }
            if (!Query.IsEmpty() && !Function->GetName().Contains(Query, ESearchCase::IgnoreCase))
            {
                continue;
            }
            TSharedPtr<FJsonObject> Hit = MakeShared<FJsonObject>();
            Hit->SetStringField(TEXT("function"), Function->GetName());
            Hit->SetStringField(TEXT("class"), Class->GetName());
            TArray<TSharedPtr<FJsonValue>> Params;
            for (TFieldIterator<FProperty> ParamIt(Function); ParamIt; ++ParamIt)
            {
                if (!ParamIt->HasAnyPropertyFlags(CPF_Parm) || ParamIt->HasAnyPropertyFlags(CPF_ReturnParm))
                {
                    continue;
                }
                TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
                Param->SetStringField(TEXT("name"), ParamIt->GetName());
                Param->SetStringField(TEXT("type"), ParamIt->GetCPPType());
                Param->SetBoolField(TEXT("output"), ParamIt->HasAnyPropertyFlags(CPF_OutParm) && !ParamIt->HasAnyPropertyFlags(CPF_ReferenceParm));
                Params.Add(MakeShared<FJsonValueObject>(Param));
            }
            Hit->SetArrayField(TEXT("params"), Params);
            Out.Add(MakeShared<FJsonValueObject>(Hit));
        }
    }
}

TSharedPtr<FJsonObject> FUnrealMCPGraphEditCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("add_blueprint_function")) return HandleAddFunction(Params);
    if (CommandType == TEXT("add_event_dispatcher")) return HandleAddEventDispatcher(Params);
    if (CommandType == TEXT("add_custom_event")) return HandleAddCustomEvent(Params);
    if (CommandType == TEXT("add_blueprint_node")) return HandleAddNode(Params);
    if (CommandType == TEXT("connect_blueprint_pins")) return HandleConnectPins(Params);
    if (CommandType == TEXT("disconnect_blueprint_pins")) return HandleDisconnectPins(Params);
    if (CommandType == TEXT("set_pin_default")) return HandleSetPinDefault(Params);
    if (CommandType == TEXT("delete_blueprint_node")) return HandleDeleteNode(Params);
    if (CommandType == TEXT("apply_blueprint_graph")) return HandleApplyGraph(Params);
    if (CommandType == TEXT("search_callable_functions")) return HandleSearchCallableFunctions(Params);
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown graph edit command: %s"), *CommandType));
}

UEdGraphNode* FUnrealMCPGraphEditCommands::CreateNode(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Spec, FString& OutError)
{
    const FString Type = ReadString(Spec, TEXT("type")).ToLower();
    const float X = ReadNumber(Spec, TEXT("x"), 0.0f);
    const float Y = ReadNumber(Spec, TEXT("y"), 0.0f);

    auto Place = [&](UEdGraphNode* Node) -> UEdGraphNode*
    {
        Node->SetFlags(RF_Transactional);
        Node->NodePosX = X;
        Node->NodePosY = Y;
        Graph->AddNode(Node, true, false);
        Node->CreateNewGuid();
        Node->PostPlacedNewNode();
        Node->AllocateDefaultPins();
        ApplyDefaults(Graph, Node, Spec);
        return Node;
    };

    if (Type == TEXT("call_function"))
    {
        const FString FunctionName = ReadString(Spec, TEXT("function"));
        const FString Target = ReadString(Spec, TEXT("target"));
        if (FunctionName.IsEmpty())
        {
            OutError = TEXT("call_function necesita 'function'");
            return nullptr;
        }
        UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
        if (Target.IsEmpty() || Target.Equals(TEXT("self"), ESearchCase::IgnoreCase))
        {
            Node->FunctionReference.SetSelfMember(FName(*FunctionName));
        }
        else
        {
            UClass* TargetClass = FUnrealMCPCommonUtils::ResolveClass(Target, OutError);
            if (!TargetClass)
            {
                return nullptr;
            }
            UFunction* Function = TargetClass->FindFunctionByName(FName(*FunctionName));
            if (!Function)
            {
                OutError = FString::Printf(TEXT("No está la función %s en %s"), *FunctionName, *TargetClass->GetName());
                return nullptr;
            }
            Node->SetFromFunction(Function);
        }
        return Place(Node);
    }
    if (Type == TEXT("variable_get") || Type == TEXT("variable_set") || Type == TEXT("component_get"))
    {
        const FString Variable = ReadString(Spec, TEXT("variable"));
        if (Variable.IsEmpty())
        {
            OutError = TEXT("Falta 'variable'");
            return nullptr;
        }
        UEdGraphNode* Node = Type == TEXT("variable_set")
            ? static_cast<UEdGraphNode*>(FUnrealMCPCommonUtils::CreateVariableSetNode(Graph, Blueprint, Variable, FVector2D(X, Y)))
            : static_cast<UEdGraphNode*>(FUnrealMCPCommonUtils::CreateVariableGetNode(Graph, Blueprint, Variable, FVector2D(X, Y)));
        if (!Node)
        {
            OutError = FString::Printf(TEXT("No se pudo crear la variable %s"), *Variable);
            return nullptr;
        }
        ApplyDefaults(Graph, Node, Spec);
        return Node;
    }
    if (Type == TEXT("branch"))
    {
        return Place(NewObject<UK2Node_IfThenElse>(Graph));
    }
    if (Type == TEXT("sequence"))
    {
        return Place(NewObject<UK2Node_ExecutionSequence>(Graph));
    }
    if (Type == TEXT("cast"))
    {
        UK2Node_DynamicCast* Node = NewObject<UK2Node_DynamicCast>(Graph);
        Node->TargetType = FUnrealMCPCommonUtils::ResolveClass(ReadString(Spec, TEXT("class")), OutError);
        if (!Node->TargetType)
        {
            return nullptr;
        }
        return Place(Node);
    }
    if (Type == TEXT("self"))
    {
        UK2Node_Self* Node = FUnrealMCPCommonUtils::CreateSelfReferenceNode(Graph, FVector2D(X, Y));
        ApplyDefaults(Graph, Node, Spec);
        return Node;
    }
    if (Type == TEXT("event"))
    {
        UEdGraphNode* Node = FUnrealMCPCommonUtils::CreateEventNode(Graph, ReadString(Spec, TEXT("name")), FVector2D(X, Y));
        if (!Node)
        {
            OutError = TEXT("No se pudo crear el evento");
        }
        return Node;
    }
    if (Type == TEXT("custom_event"))
    {
        UK2Node_CustomEvent* Node = NewObject<UK2Node_CustomEvent>(Graph);
        Node->CustomFunctionName = FName(*ReadString(Spec, TEXT("name")));
        Place(Node);
        const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
        Spec->TryGetArrayField(TEXT("params"), Params);
        if (!AddUserPins(Node, Params, EGPD_Output, OutError))
        {
            return nullptr;
        }
        return Node;
    }
    if (Type == TEXT("call_dispatcher") || Type == TEXT("bind_dispatcher"))
    {
        UK2Node_BaseMCDelegate* Node = Type == TEXT("call_dispatcher")
            ? static_cast<UK2Node_BaseMCDelegate*>(NewObject<UK2Node_CallDelegate>(Graph))
            : static_cast<UK2Node_BaseMCDelegate*>(NewObject<UK2Node_AddDelegate>(Graph));
        Node->DelegateReference.SetSelfMember(FName(*ReadString(Spec, TEXT("dispatcher"))));
        return Place(Node);
    }
    if (Type == TEXT("enhanced_input_action"))
    {
        FString ActionError;
        const FString ActionPath = ReadString(Spec, TEXT("action"));
        UInputAction* Action = LoadObject<UInputAction>(nullptr, *UnrealMCPGraph::NormalizeObjectPath(ActionPath));
        if (!Action)
        {
            OutError = FString::Printf(TEXT("No se encontró el Input Action %s"), *ActionPath);
            return nullptr;
        }
        UK2Node_EnhancedInputAction* Node = NewObject<UK2Node_EnhancedInputAction>(Graph);
        Node->InputAction = Action;
        return Place(Node);
    }
    if (Type == TEXT("macro"))
    {
        const FString MacroName = ReadString(Spec, TEXT("macro"));
        UBlueprint* MacroLibrary = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
        UEdGraph* MacroGraph = nullptr;
        if (MacroLibrary)
        {
            for (UEdGraph* Candidate : MacroLibrary->MacroGraphs)
            {
                if (Candidate && Candidate->GetName().Equals(MacroName, ESearchCase::IgnoreCase))
                {
                    MacroGraph = Candidate;
                    break;
                }
            }
        }
        if (!MacroGraph)
        {
            OutError = FString::Printf(TEXT("No está el macro '%s'"), *MacroName);
            return nullptr;
        }
        UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
        Node->SetMacroGraph(MacroGraph);
        return Place(Node);
    }
    if (Type == TEXT("knot"))
    {
        return Place(NewObject<UK2Node_Knot>(Graph));
    }

    OutError = FString::Printf(TEXT("Tipo de nodo desconocido: %s"), *Type);
    return nullptr;
}

TSharedPtr<FJsonObject> FUnrealMCPGraphEditCommands::HandleAddFunction(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName = ReadString(Params, TEXT("blueprint_path"));
    if (BlueprintName.IsEmpty())
    {
        BlueprintName = ReadString(Params, TEXT("blueprint_name"));
    }
    const FString Name = ReadString(Params, TEXT("name"));
    FString FindError;
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName, FindError);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FindError);
    }
    bool bCollapsed = false;
    if (Name.IsEmpty() || FUnrealMCPCommonUtils::FindGraph(Blueprint, Name, bCollapsed))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'name' o la función ya existe"));
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP add function"));
    Transaction.Modify(Blueprint);
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);

    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Entry)
        {
            Entry = Cast<UK2Node_FunctionEntry>(Node);
        }
        if (!Result)
        {
            Result = Cast<UK2Node_FunctionResult>(Node);
        }
    }
    if (!Result)
    {
        Result = NewObject<UK2Node_FunctionResult>(Graph);
        Result->NodePosX = 400;
        Graph->AddNode(Result, true, false);
        Result->CreateNewGuid();
        Result->PostPlacedNewNode();
        // PostPlacedNewNode ya sincroniza con la entrada y crea los pines; volver a crearlos los duplica
        if (Result->Pins.Num() == 0)
        {
            Result->AllocateDefaultPins();
        }
    }
    FString PinError;
    const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
    Params->TryGetArrayField(TEXT("inputs"), Inputs);
    Params->TryGetArrayField(TEXT("outputs"), Outputs);
    if (!AddUserPins(Entry, Inputs, EGPD_Output, PinError) || !AddUserPins(Result, Outputs, EGPD_Input, PinError))
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(PinError);
    }
    bool bPure = false;
    if (Params->HasField(TEXT("pure")))
    {
        bPure = Params->GetBoolField(TEXT("pure"));
    }
    if (bPure && Entry)
    {
        Entry->AddExtraFlags(FUNC_BlueprintPure);
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Name);
    Data->SetStringField(TEXT("graph"), Graph->GetName());
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPGraphEditCommands::HandleAddEventDispatcher(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName = ReadString(Params, TEXT("blueprint_path"));
    if (BlueprintName.IsEmpty())
    {
        BlueprintName = ReadString(Params, TEXT("blueprint_name"));
    }
    const FString Name = ReadString(Params, TEXT("name"));
    FString FindError;
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName, FindError);
    if (!Blueprint || Name.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FindError.IsEmpty() ? TEXT("Falta 'name'") : FindError);
    }
    for (UEdGraph* Existing : Blueprint->DelegateSignatureGraphs)
    {
        if (Existing && Existing->GetName().Equals(Name, ESearchCase::IgnoreCase))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("El dispatcher ya existe"));
        }
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP add dispatcher"));
    Transaction.Modify(Blueprint);
    FEdGraphPinType DelegateType;
    DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
    if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Name), DelegateType))
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se pudo crear la variable del dispatcher"));
    }
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    if (!Graph)
    {
        FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, FName(*Name));
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se pudo crear el grafo del dispatcher"));
    }
    Graph->bEditable = false;
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    Schema->CreateDefaultNodesForGraph(*Graph);
    Schema->CreateFunctionGraphTerminators(*Graph, static_cast<UClass*>(nullptr));
    Schema->AddExtraFunctionFlags(Graph, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
    Schema->MarkFunctionEntryAsEditable(Graph, true);
    Blueprint->DelegateSignatureGraphs.Add(Graph);
    UK2Node_FunctionEntry* Entry = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        Entry = Cast<UK2Node_FunctionEntry>(Node);
        if (Entry)
        {
            break;
        }
    }
    FString PinError;
    const TArray<TSharedPtr<FJsonValue>>* PinSpecs = nullptr;
    Params->TryGetArrayField(TEXT("params"), PinSpecs);
    if (!AddUserPins(Entry, PinSpecs, EGPD_Output, PinError))
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(PinError);
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("name"), Name);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPGraphEditCommands::HandleAddCustomEvent(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName = ReadString(Params, TEXT("blueprint_path"));
    if (BlueprintName.IsEmpty())
    {
        BlueprintName = ReadString(Params, TEXT("blueprint_name"));
    }
    FString FindError;
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName, FindError);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FindError);
    }
    FString GraphError;
    UEdGraph* Graph = FUnrealMCPCommonUtils::ResolveGraph(Blueprint, Params, GraphError);
    if (!Graph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(GraphError);
    }
    TSharedPtr<FJsonObject> Spec = MakeShared<FJsonObject>();
    Spec->SetStringField(TEXT("type"), TEXT("custom_event"));
    Spec->SetStringField(TEXT("name"), ReadString(Params, TEXT("name")));
    const TArray<TSharedPtr<FJsonValue>>* PinSpecs = nullptr;
    if (Params->TryGetArrayField(TEXT("params"), PinSpecs))
    {
        Spec->SetArrayField(TEXT("params"), *PinSpecs);
    }
    FUnrealMCPEditTransaction Transaction(TEXT("MCP add custom event"));
    Transaction.Modify(Blueprint);
    Transaction.Modify(Graph);
    FString CreateError;
    UEdGraphNode* Node = CreateNode(Blueprint, Graph, Spec, CreateError);
    if (!Node)
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(CreateError);
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("node_id"), NodeId(Node));
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPGraphEditCommands::HandleAddNode(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName = ReadString(Params, TEXT("blueprint_path"));
    if (BlueprintName.IsEmpty())
    {
        BlueprintName = ReadString(Params, TEXT("blueprint_name"));
    }
    FString FindError;
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName, FindError);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FindError);
    }
    FString GraphError;
    UEdGraph* Graph = FUnrealMCPCommonUtils::ResolveGraph(Blueprint, Params, GraphError);
    if (!Graph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(GraphError);
    }
    FUnrealMCPEditTransaction Transaction(TEXT("MCP add node"));
    Transaction.Modify(Blueprint);
    Transaction.Modify(Graph);
    FString CreateError;
    UEdGraphNode* Node = CreateNode(Blueprint, Graph, Params, CreateError);
    if (!Node)
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(CreateError);
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("node_id"), NodeId(Node));
    Data->SetStringField(TEXT("class"), Node->GetClass()->GetName());
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPGraphEditCommands::HandleConnectPins(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName = ReadString(Params, TEXT("blueprint_path"));
    if (BlueprintName.IsEmpty())
    {
        BlueprintName = ReadString(Params, TEXT("blueprint_name"));
    }
    FString FindError;
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName, FindError);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FindError);
    }
    FString GraphError;
    UEdGraph* Graph = FUnrealMCPCommonUtils::ResolveGraph(Blueprint, Params, GraphError);
    if (!Graph || !Graph->GetSchema())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(GraphError.IsEmpty() ? TEXT("El grafo no tiene schema") : GraphError);
    }

    FString FromNode;
    FString FromPin;
    FString ToNode;
    FString ToPin;
    if (!SplitEndpoint(ReadString(Params, TEXT("from")), FromNode, FromPin) || !SplitEndpoint(ReadString(Params, TEXT("to")), ToNode, ToPin))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'from' y 'to' tienen que ser nodo.pin"));
    }
    TMap<FString, UEdGraphNode*> Empty;
    UEdGraphNode* Source = FindNode(Graph, Empty, FromNode);
    UEdGraphNode* Target = FindNode(Graph, Empty, ToNode);
    UEdGraphPin* SourcePin = Source ? FindPinEither(Source, FromPin) : nullptr;
    UEdGraphPin* TargetPin = Target ? FindPinEither(Target, ToPin) : nullptr;
    if (!SourcePin || !TargetPin)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se encontró un nodo o un pin"));
    }

    const FPinConnectionResponse Response = Graph->GetSchema()->CanCreateConnection(SourcePin, TargetPin);
    if (Response.Response == CONNECT_RESPONSE_DISALLOW)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Response.Message.ToString());
    }
    FUnrealMCPEditTransaction Transaction(TEXT("MCP connect pins"));
    Transaction.Modify(Blueprint);
    Transaction.Modify(Graph);
    if (!Graph->GetSchema()->TryCreateConnection(SourcePin, TargetPin))
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("TryCreateConnection rechazó la conexión"));
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("from"), ReadString(Params, TEXT("from")));
    Data->SetStringField(TEXT("to"), ReadString(Params, TEXT("to")));
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPGraphEditCommands::HandleDisconnectPins(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName = ReadString(Params, TEXT("blueprint_path"));
    if (BlueprintName.IsEmpty())
    {
        BlueprintName = ReadString(Params, TEXT("blueprint_name"));
    }
    FString FindError;
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName, FindError);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FindError);
    }
    FString GraphError;
    UEdGraph* Graph = FUnrealMCPCommonUtils::ResolveGraph(Blueprint, Params, GraphError);
    if (!Graph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(GraphError);
    }
    FString FromNode;
    FString FromPin;
    if (!SplitEndpoint(ReadString(Params, TEXT("from")), FromNode, FromPin))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'from' tiene que ser nodo.pin"));
    }
    TMap<FString, UEdGraphNode*> Empty;
    UEdGraphNode* Source = FindNode(Graph, Empty, FromNode);
    UEdGraphPin* Pin = Source ? FindPinEither(Source, FromPin) : nullptr;
    if (!Pin)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se encontró el pin"));
    }
    FUnrealMCPEditTransaction Transaction(TEXT("MCP disconnect pin"));
    Transaction.Modify(Graph);
    Pin->BreakAllPinLinks();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(MakeShared<FJsonObject>());
}

TSharedPtr<FJsonObject> FUnrealMCPGraphEditCommands::HandleSetPinDefault(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName = ReadString(Params, TEXT("blueprint_path"));
    if (BlueprintName.IsEmpty())
    {
        BlueprintName = ReadString(Params, TEXT("blueprint_name"));
    }
    FString FindError;
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName, FindError);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FindError);
    }
    FString GraphError;
    UEdGraph* Graph = FUnrealMCPCommonUtils::ResolveGraph(Blueprint, Params, GraphError);
    if (!Graph || !Graph->GetSchema())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(GraphError);
    }
    FString NodeText;
    FString PinText;
    if (!SplitEndpoint(ReadString(Params, TEXT("pin")), NodeText, PinText))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("'pin' tiene que ser nodo.pin"));
    }
    TMap<FString, UEdGraphNode*> Empty;
    UEdGraphNode* Node = FindNode(Graph, Empty, NodeText);
    UEdGraphPin* Pin = Node ? FindPinEither(Node, PinText) : nullptr;
    if (!Pin)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se encontró el pin"));
    }
    FUnrealMCPEditTransaction Transaction(TEXT("MCP set pin default"));
    Transaction.Modify(Graph);
    Graph->GetSchema()->TrySetDefaultValue(*Pin, ReadString(Params, TEXT("value")));
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(MakeShared<FJsonObject>());
}

TSharedPtr<FJsonObject> FUnrealMCPGraphEditCommands::HandleDeleteNode(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName = ReadString(Params, TEXT("blueprint_path"));
    if (BlueprintName.IsEmpty())
    {
        BlueprintName = ReadString(Params, TEXT("blueprint_name"));
    }
    FString FindError;
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName, FindError);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FindError);
    }
    FString GraphError;
    UEdGraph* Graph = FUnrealMCPCommonUtils::ResolveGraph(Blueprint, Params, GraphError);
    if (!Graph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(GraphError);
    }
    TMap<FString, UEdGraphNode*> Empty;
    UEdGraphNode* Node = FindNode(Graph, Empty, ReadString(Params, TEXT("node_id")));
    if (!Node)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se encontró el nodo"));
    }
    FUnrealMCPEditTransaction Transaction(TEXT("MCP delete node"));
    Transaction.Modify(Blueprint);
    Transaction.Modify(Graph);
    FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(MakeShared<FJsonObject>());
}

TSharedPtr<FJsonObject> FUnrealMCPGraphEditCommands::HandleApplyGraph(const TSharedPtr<FJsonObject>& Params)
{
    const FString BlueprintName = ReadString(Params, TEXT("blueprint_path"));
    const FString GraphName = ReadString(Params, TEXT("graph_name"));
    FString FindError;
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName, FindError);
    if (!Blueprint || GraphName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FindError.IsEmpty() ? TEXT("Faltan 'blueprint_path' y 'graph_name'") : FindError);
    }

    bool bCreate = false;
    if (Params->HasField(TEXT("create_graph_if_missing")))
    {
        bCreate = Params->GetBoolField(TEXT("create_graph_if_missing"));
    }
    bool bCollapsed = false;
    UEdGraph* Graph = FUnrealMCPCommonUtils::FindGraph(Blueprint, GraphName, bCollapsed);
    FUnrealMCPEditTransaction Transaction(TEXT("MCP apply blueprint graph"));
    Transaction.Modify(Blueprint);
    if (!Graph && bCreate)
    {
        Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*GraphName), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
    }
    if (!Graph)
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No está el grafo '%s'"), *GraphName));
    }
    Transaction.Modify(Graph);

    const TArray<TSharedPtr<FJsonValue>>* NodeValues = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* LinkValues = nullptr;
    Params->TryGetArrayField(TEXT("nodes"), NodeValues);
    Params->TryGetArrayField(TEXT("links"), LinkValues);

    TMap<FString, UEdGraphNode*> Refs;
    TArray<FString> Errors;
    if (NodeValues)
    {
        for (const TSharedPtr<FJsonValue>& Value : *NodeValues)
        {
            const TSharedPtr<FJsonObject> Spec = Value->AsObject();
            const FString Ref = ReadString(Spec, TEXT("ref"));
            FString CreateError;
            UEdGraphNode* Node = CreateNode(Blueprint, Graph, Spec, CreateError);
            if (!Node)
            {
                Errors.Add(Ref.IsEmpty() ? CreateError : Ref + TEXT(": ") + CreateError);
                continue;
            }
            if (!Ref.IsEmpty())
            {
                Refs.Add(Ref, Node);
            }
        }
    }
    if (Errors.Num() == 0 && LinkValues)
    {
        for (const TSharedPtr<FJsonValue>& Value : *LinkValues)
        {
            const TSharedPtr<FJsonObject> Link = Value->AsObject();
            FString FromNode;
            FString FromPin;
            FString ToNode;
            FString ToPin;
            if (!SplitEndpoint(ReadString(Link, TEXT("from")), FromNode, FromPin) || !SplitEndpoint(ReadString(Link, TEXT("to")), ToNode, ToPin))
            {
                Errors.Add(TEXT("Un cable no tiene la forma nodo.pin"));
                continue;
            }
            UEdGraphNode* Source = FindNode(Graph, Refs, FromNode);
            UEdGraphNode* Target = FindNode(Graph, Refs, ToNode);
            UEdGraphPin* SourcePin = Source ? FindPinEither(Source, FromPin) : nullptr;
            UEdGraphPin* TargetPin = Target ? FindPinEither(Target, ToPin) : nullptr;
            if (!SourcePin || !TargetPin)
            {
                Errors.Add(FString::Printf(TEXT("No se encontró %s o %s"), *ReadString(Link, TEXT("from")), *ReadString(Link, TEXT("to"))));
                continue;
            }
            const FPinConnectionResponse Response = Graph->GetSchema()->CanCreateConnection(SourcePin, TargetPin);
            if (Response.Response == CONNECT_RESPONSE_DISALLOW || !Graph->GetSchema()->TryCreateConnection(SourcePin, TargetPin))
            {
                Errors.Add(Response.Message.IsEmpty()
                    ? FString::Printf(TEXT("No se pudo conectar %s"), *ReadString(Link, TEXT("from")))
                    : Response.Message.ToString());
            }
        }
    }
    if (Errors.Num() > 0)
    {
        Transaction.Cancel();
        TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> ErrorValues;
        for (const FString& Error : Errors)
        {
            ErrorValues.Add(MakeShared<FJsonValueString>(Error));
        }
        Data->SetArrayField(TEXT("errors"), ErrorValues);
        TSharedPtr<FJsonObject> Failure = FUnrealMCPCommonUtils::CreateErrorResponse(FString::Join(Errors, TEXT(" | ")));
        Failure->SetObjectField(TEXT("data"), Data);
        return Failure;
    }

    bool bCompile = true;
    if (Params->HasField(TEXT("compile")))
    {
        bCompile = Params->GetBoolField(TEXT("compile"));
    }
    TArray<TSharedPtr<FJsonValue>> Messages;
    if (bCompile)
    {
        Messages = CompileBlueprint(Blueprint);
    }
    else
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }

    TSharedPtr<FJsonObject> RefMap = MakeShared<FJsonObject>();
    for (const TPair<FString, UEdGraphNode*>& Pair : Refs)
    {
        RefMap->SetStringField(Pair.Key, NodeId(Pair.Value));
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("blueprint"), Blueprint->GetName());
    Data->SetStringField(TEXT("path"), Blueprint->GetPathName());
    Data->SetStringField(TEXT("parent"), Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT(""));
    Data->SetObjectField(TEXT("refs"), RefMap);
    Data->SetArrayField(TEXT("compile_messages"), Messages);
    TArray<TSharedPtr<FJsonValue>> Graphs;
    Graphs.Add(MakeShared<FJsonValueObject>(UnrealMCPGraph::GraphToJson(Graph)));
    Data->SetArrayField(TEXT("graphs"), Graphs);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPGraphEditCommands::HandleSearchCallableFunctions(const TSharedPtr<FJsonObject>& Params)
{
    const FString Query = ReadString(Params, TEXT("query"));
    const FString ClassName = ReadString(Params, TEXT("class"));
    TArray<TSharedPtr<FJsonValue>> Hits;
    if (!ClassName.IsEmpty())
    {
        FString ResolveError;
        UClass* Class = FUnrealMCPCommonUtils::ResolveClass(ClassName, ResolveError);
        if (!Class)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ResolveError);
        }
        AddFunctionHits(Class, Query, Hits);
    }
    else
    {
        const FString BlueprintName = ReadString(Params, TEXT("blueprint_path"));
        if (!BlueprintName.IsEmpty())
        {
            FString FindError;
            if (UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName, FindError))
            {
                AddFunctionHits(Blueprint->GeneratedClass, Query, Hits);
            }
        }
        AddFunctionHits(UKismetSystemLibrary::StaticClass(), Query, Hits);
        AddFunctionHits(UGameplayStatics::StaticClass(), Query, Hits);
        AddFunctionHits(UKismetMathLibrary::StaticClass(), Query, Hits);
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("query"), Query);
    Data->SetNumberField(TEXT("count"), Hits.Num());
    Data->SetArrayField(TEXT("functions"), Hits);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}
