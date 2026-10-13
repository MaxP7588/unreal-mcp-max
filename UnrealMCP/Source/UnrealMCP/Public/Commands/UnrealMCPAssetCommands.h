#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UNREALMCP_API FUnrealMCPAssetCommands
{
public:
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleDuplicateAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSaveAssets(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFindAssetReferences(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetEditorLog(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSearchAssets(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAssetInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRenameAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDeleteAsset(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleCreateFolder(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleImportAsset(const TSharedPtr<FJsonObject>& Params);
};
