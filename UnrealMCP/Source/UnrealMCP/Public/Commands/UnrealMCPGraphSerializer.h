#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

/**
 * Read-only Blueprint graph serialization shared by the dump and the read tools.
 */
namespace UnrealMCPGraph
{
    FString NormalizeObjectPath(const FString& InPath);
    bool IsOutputPathInsideProject(const FString& FullPath);
    UBlueprint* ResolveBlueprint(const FString& ObjectPath, FString& OutError);

    void CollectBlueprintGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs);

    TSharedPtr<FJsonObject> PinToJson(const UEdGraphPin* Pin);
    TSharedPtr<FJsonObject> NodeToJson(UEdGraphNode* Node);
    TSharedPtr<FJsonObject> GraphToJson(UEdGraph* Graph);
}
