#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Json.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "UnrealMCPBridge.generated.h"

class FMCPServerRunnable;

UNREALMCP_API DECLARE_LOG_CATEGORY_EXTERN(LogUnrealMCP, Log, All);

/** Recibe (comando, params) y devuelve el JSON de resultado. */
using FUnrealMCPCommandHandler = TFunction<TSharedPtr<FJsonObject>(const FString&, const TSharedPtr<FJsonObject>&)>;

/**
 * Editor subsystem for MCP Bridge.
 * Recibe comandos JSON por TCP y los despacha en el game thread a través de un registro
 * de comandos agrupados por toolset.
 */
UCLASS()
class UNREALMCP_API UUnrealMCPBridge : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UUnrealMCPBridge();
	virtual ~UUnrealMCPBridge();

	// UEditorSubsystem implementation
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Server functions
	void StartServer();
	void StopServer();
	bool IsRunning() const { return bIsRunning; }

	/** Ejecuta un comando en el game thread y espera con timeout. Seguro de llamar desde cualquier hilo. */
	FString ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

	/** Registra comandos para un toolset. Un nombre repetido reemplaza al anterior. */
	void RegisterCommands(const FString& Toolset, const TArray<FString>& Commands, FUnrealMCPCommandHandler Handler);

private:
	struct FCommandEntry
	{
		FString Toolset;
		FUnrealMCPCommandHandler Handler;
	};

	void RegisterAllCommands();
	TSharedPtr<FJsonObject> DispatchOnGameThread(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListCommands() const;

	static FString MakeErrorResponse(const FString& Message);

	// Server state
	bool bIsRunning = false;
	TSharedPtr<FSocket> ListenerSocket;
	FMCPServerRunnable* ServerRunnable = nullptr;
	FRunnableThread* ServerThread = nullptr;

	// Server configuration
	FIPv4Address ServerAddress;
	uint16 Port = 55557;

	TMap<FString, FCommandEntry> CommandRegistry;

	// Los handlers se mantienen vivos mientras viva el subsystem.
	TArray<TSharedPtr<void>> HandlerOwners;
};
