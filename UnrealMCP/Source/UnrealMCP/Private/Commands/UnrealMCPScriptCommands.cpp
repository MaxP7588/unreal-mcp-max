#include "Commands/UnrealMCPScriptCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "IPythonScriptPlugin.h"
#include "Editor.h"
#include "Misc/OutputDeviceRedirector.h"

namespace
{
    constexpr int32 MaxOutputChars = 60000;

    FString Truncate(const FString& Text)
    {
        if (Text.Len() <= MaxOutputChars)
        {
            return Text;
        }
        return Text.Left(MaxOutputChars) + FString::Printf(TEXT("\n... (%d caracteres más)"), Text.Len() - MaxOutputChars);
    }

    /** Captura todo lo que se escribe al log mientras vive. */
    class FScopedLogCapture : public FOutputDevice
    {
    public:
        FScopedLogCapture() { GLog->AddOutputDevice(this); }
        virtual ~FScopedLogCapture() override { GLog->RemoveOutputDevice(this); }

        virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
        {
            if (Category == TEXT("LogUnrealMCP"))
            {
                return;
            }
            Lines.Add(FString::Printf(TEXT("[%s] %s"), *Category.ToString(), V));
        }

        TArray<FString> Lines;
    };
}

TSharedPtr<FJsonObject> FUnrealMCPScriptCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("execute_python"))
    {
        return HandleExecutePython(Params);
    }
    if (CommandType == TEXT("console_command"))
    {
        return HandleConsoleCommand(Params);
    }
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Comando de script desconocido: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPScriptCommands::HandleExecutePython(const TSharedPtr<FJsonObject>& Params)
{
    FString Code;
    if (!Params->TryGetStringField(TEXT("code"), Code) || Code.TrimStartAndEnd().IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'code'"));
    }

    IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
    if (!Python || !Python->IsPythonAvailable())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Python no está disponible: activa el plugin 'Python Editor Script Plugin' y reinicia el editor"));
    }

    // "file" ejecuta varias líneas; "eval" devuelve el valor de una expresión.
    FString Mode = TEXT("file");
    Params->TryGetStringField(TEXT("mode"), Mode);

    FPythonCommandEx Command;
    Command.Command = Code;
    Command.FileExecutionScope = EPythonFileExecutionScope::Public;
    Command.ExecutionMode = Mode == TEXT("eval") ? EPythonCommandExecutionMode::EvaluateStatement : EPythonCommandExecutionMode::ExecuteFile;

    // Desactivado por defecto: abrir un nivel dentro de una transacción no es seguro.
    bool bTransaction = false;
    Params->TryGetBoolField(TEXT("undoable"), bTransaction);
    TUniquePtr<FUnrealMCPEditTransaction> Transaction;
    if (bTransaction)
    {
        Transaction = MakeUnique<FUnrealMCPEditTransaction>(TEXT("MCP: Python"));
    }

    const bool bOk = Python->ExecPythonCommandEx(Command);

    FString Output;
    FString Errors;
    for (const FPythonLogOutputEntry& Entry : Command.LogOutput)
    {
        if (Entry.Type == EPythonLogOutputType::Error)
        {
            Errors += Entry.Output + TEXT("\n");
        }
        else
        {
            Output += Entry.Output + TEXT("\n");
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("output"), Truncate(Output));
    if (!Command.CommandResult.IsEmpty() && Command.CommandResult != TEXT("None"))
    {
        Result->SetStringField(TEXT("result"), Truncate(Command.CommandResult));
    }

    if (!bOk)
    {
        TSharedPtr<FJsonObject> Error = FUnrealMCPCommonUtils::CreateErrorResponse(
            Truncate(FString::Printf(TEXT("Error de Python:\n%s%s\nSalida:\n%s"), *Errors, *Command.CommandResult, *Output)));
        return Error;
    }
    if (!Errors.IsEmpty())
    {
        Result->SetStringField(TEXT("errors"), Truncate(Errors));
    }
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPScriptCommands::HandleConsoleCommand(const TSharedPtr<FJsonObject>& Params)
{
    FString CommandLine;
    if (!Params->TryGetStringField(TEXT("command"), CommandLine) || CommandLine.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'command'"));
    }

    bool bPIE = false;
    Params->TryGetBoolField(TEXT("pie"), bPIE);
    UWorld* World = FUnrealMCPCommonUtils::GetEditorWorld(bPIE);

    // La salida de muchos comandos va al log y no al dispositivo, así que se capturan ambos.
    FStringOutputDevice Device;
    Device.SetAutoEmitLineTerminator(true);
    bool bHandled = false;
    TArray<FString> LogLines;
    {
        FScopedLogCapture Capture;
        bHandled = GEditor->Exec(World, *CommandLine, Device);
        LogLines = MoveTemp(Capture.Lines);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("command"), CommandLine);
    Result->SetBoolField(TEXT("handled"), bHandled);
    Result->SetStringField(TEXT("output"), Truncate(Device));
    Result->SetStringField(TEXT("log"), Truncate(FString::Join(LogLines, TEXT("\n"))));
    return Result;
}
