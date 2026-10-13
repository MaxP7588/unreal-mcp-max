#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Ejecución de Python (PythonScriptPlugin) y comandos de consola del editor.
 * Sirve para todo lo que no tiene una herramienta propia.
 */
class UNREALMCP_API FUnrealMCPScriptCommands
{
public:
    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleExecutePython(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleConsoleCommand(const TSharedPtr<FJsonObject>& Params);
};
