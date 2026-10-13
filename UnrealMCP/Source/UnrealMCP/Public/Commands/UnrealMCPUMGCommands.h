#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UWidgetBlueprint;
class UWidget;

/**
 * Widget Blueprints (UMG): creación, árbol de widgets, propiedades y eventos.
 * Los widgets se buscan por nombre corto o ruta completa en todo /Game.
 */
class UNREALMCP_API FUnrealMCPUMGCommands
{
public:
    FUnrealMCPUMGCommands();

    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleCreateUMGWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddWidget(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetWidgetTree(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetWidgetProperty(const TSharedPtr<FJsonObject>& Params);

    // Atajos sobre add_widget
    TSharedPtr<FJsonObject> HandleAddTextBlockToWidget(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddButtonToWidget(const TSharedPtr<FJsonObject>& Params);

    TSharedPtr<FJsonObject> HandleAddWidgetToViewport(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleBindWidgetEvent(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetTextBlockBinding(const TSharedPtr<FJsonObject>& Params);

    /** Crea el widget, lo agrega al padre y aplica propiedades/slot. Devuelve null y OutError si falla. */
    UWidget* AddWidgetInternal(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetType, const FString& WidgetName,
                               const TSharedPtr<FJsonObject>& Params, FString& OutError);
};
