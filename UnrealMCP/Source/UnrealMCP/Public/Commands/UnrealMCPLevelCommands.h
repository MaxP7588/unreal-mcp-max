#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Nivel, selección, PIE, cámara del viewport y propiedades genéricas de cualquier objeto.
 */
class UNREALMCP_API FUnrealMCPLevelCommands
{
public:
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleGetActorComponents(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSelectedActors(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetSelection(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetActorLabel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetActorFolder(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetActorTags(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleDuplicateActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAttachActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetLevelInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleOpenLevel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSaveLevel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListLevels(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleStartPIE(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleStopPIE(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleIsPIERunning(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetViewportCamera(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetViewportCamera(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFocusActor(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetObjectProperties(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetObjectProperty(const TSharedPtr<FJsonObject>& Params);
};
