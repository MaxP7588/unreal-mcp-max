#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UNREALMCP_API FUnrealMCPInputCommands
{
public:
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleReadInputAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFindInputBindings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddEnhancedInputMapping(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveEnhancedInputMapping(const TSharedPtr<FJsonObject>& Params);
};
