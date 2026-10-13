#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Sockets.h"

class UUnrealMCPBridge;

/**
 * Hilo del servidor MCP.
 *
 * Protocolo: cada mensaje es un objeto JSON {"type": ..., "params": {...}} terminado en '\n'.
 * Si el cliente no manda '\n' (clientes antiguos), se intenta parsear el buffer acumulado como
 * JSON completo. Cada respuesta es un JSON terminado en '\n'. Una conexión puede mandar varios
 * mensajes; si llega un cliente nuevo, reemplaza al anterior.
 */
class FMCPServerRunnable : public FRunnable
{
public:
	FMCPServerRunnable(UUnrealMCPBridge* InBridge, TSharedPtr<FSocket> InListenerSocket);
	virtual ~FMCPServerRunnable();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

private:
	void AcceptPendingClient();
	void CloseClient();
	/** Devuelve false si la conexión se cerró. */
	bool ReadFromClient();
	void ProcessBuffer();
	void HandleMessage(const FString& Message);
	bool SendResponse(const FString& Response);

	UUnrealMCPBridge* Bridge;
	TSharedPtr<FSocket> ListenerSocket;
	FSocket* ClientSocket = nullptr;
	TArray<uint8> PendingBytes;
	FThreadSafeBool bRunning;
};
