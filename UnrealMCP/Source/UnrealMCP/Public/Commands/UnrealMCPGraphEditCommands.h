#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

class UNREALMCP_API FUnrealMCPGraphEditCommands
{
public:
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

    static UEdGraphNode* CreateNode(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Spec, FString& OutError);

private:
    TSharedPtr<FJsonObject> HandleAddFunction(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddEventDispatcher(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddCustomEvent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConnectPins(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDisconnectPins(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPinDefault(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteNode(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleApplyGraph(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSearchCallableFunctions(const TSharedPtr<FJsonObject>& Params);
};
