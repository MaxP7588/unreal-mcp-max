#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Read-only Blueprint, graph, and data-asset commands.
 * These never modify assets.
 */
class UNREALMCP_API FUnrealMCPBlueprintReadCommands
{
public:
    FUnrealMCPBlueprintReadCommands();

    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleListBlueprints(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetBlueprintOverview(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadBlueprintGraph(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFindInBlueprints(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleExportBlueprintNodesText(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleReadDataAsset(const TSharedPtr<FJsonObject>& Params);
};
