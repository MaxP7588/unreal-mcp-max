#include "MCPServerRunnable.h"
#include "UnrealMCPBridge.h"
#include "UnrealMCPSettings.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

namespace
{
	constexpr int32 MaxMessageBytes = 64 * 1024 * 1024;
	constexpr int32 RecvChunkBytes = 64 * 1024;

	FString Utf8BytesToString(const uint8* Data, int32 Num)
	{
		const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Data), Num);
		return FString(Converter.Length(), Converter.Get());
	}

	FString ErrorJson(const FString& Message)
	{
		TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("status"), TEXT("error"));
		Response->SetStringField(TEXT("error"), Message);
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
		return Out;
	}
}

FMCPServerRunnable::FMCPServerRunnable(UUnrealMCPBridge* InBridge, TSharedPtr<FSocket> InListenerSocket)
	: Bridge(InBridge)
	, ListenerSocket(InListenerSocket)
	, bRunning(true)
{
}

FMCPServerRunnable::~FMCPServerRunnable()
{
	CloseClient();
}

bool FMCPServerRunnable::Init()
{
	return true;
}

uint32 FMCPServerRunnable::Run()
{
	while (bRunning)
	{
		AcceptPendingClient();

		if (!ClientSocket)
		{
			FPlatformProcess::Sleep(0.05f);
			continue;
		}

		if (!ClientSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(50)))
		{
			continue;
		}

		if (!ReadFromClient())
		{
			CloseClient();
			continue;
		}
		ProcessBuffer();
	}

	CloseClient();
	return 0;
}

void FMCPServerRunnable::Stop()
{
	bRunning = false;
}

void FMCPServerRunnable::Exit()
{
}

void FMCPServerRunnable::AcceptPendingClient()
{
	bool bPending = false;
	if (!ListenerSocket.IsValid() || !ListenerSocket->HasPendingConnection(bPending) || !bPending)
	{
		return;
	}

	FSocket* NewClient = ListenerSocket->Accept(TEXT("UnrealMCPClient"));
	if (!NewClient)
	{
		return;
	}

	if (ClientSocket)
	{
		// Un cliente nuevo (p. ej. el servidor Python reiniciado) reemplaza al anterior.
		UE_LOG(LogUnrealMCP, Display, TEXT("Cliente nuevo: se cierra la conexión anterior"));
		CloseClient();
	}

	NewClient->SetNonBlocking(true);
	NewClient->SetNoDelay(true);
	int32 ActualSize = 0;
	NewClient->SetSendBufferSize(1024 * 1024, ActualSize);
	NewClient->SetReceiveBufferSize(1024 * 1024, ActualSize);
	ClientSocket = NewClient;
	PendingBytes.Reset();
	UE_LOG(LogUnrealMCP, Verbose, TEXT("Cliente conectado"));
}

void FMCPServerRunnable::CloseClient()
{
	if (ClientSocket)
	{
		ClientSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ClientSocket);
		ClientSocket = nullptr;
	}
	PendingBytes.Reset();
}

bool FMCPServerRunnable::ReadFromClient()
{
	uint8 Chunk[RecvChunkBytes];
	bool bReadAnything = false;

	while (true)
	{
		int32 BytesRead = 0;
		const bool bOk = ClientSocket->Recv(Chunk, RecvChunkBytes, BytesRead);
		if (!bOk)
		{
			const ESocketErrors Error = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
			if (Error == SE_EWOULDBLOCK || Error == SE_EINTR)
			{
				return true;
			}
			return false;
		}
		if (BytesRead <= 0)
		{
			// Wait() dijo que había datos pero Recv leyó 0: el cliente cerró.
			return bReadAnything;
		}

		bReadAnything = true;
		PendingBytes.Append(Chunk, BytesRead);
		if (PendingBytes.Num() > MaxMessageBytes)
		{
			SendResponse(ErrorJson(TEXT("Mensaje demasiado grande (máx. 64 MB)")));
			return false;
		}

		uint32 PendingSize = 0;
		if (!ClientSocket->HasPendingData(PendingSize) || PendingSize == 0)
		{
			return true;
		}
	}
}

void FMCPServerRunnable::ProcessBuffer()
{
	// Mensajes terminados en '\n'.
	int32 Start = 0;
	for (int32 Index = 0; Index < PendingBytes.Num(); ++Index)
	{
		if (PendingBytes[Index] == '\n')
		{
			const FString Line = Utf8BytesToString(PendingBytes.GetData() + Start, Index - Start).TrimStartAndEnd();
			Start = Index + 1;
			if (!Line.IsEmpty())
			{
				HandleMessage(Line);
			}
		}
	}
	if (Start > 0)
	{
		PendingBytes.RemoveAt(0, Start, EAllowShrinking::No);
	}

	// Clientes antiguos que no mandan '\n': procesar si el buffer ya es un JSON completo.
	if (PendingBytes.Num() > 0)
	{
		const FString Text = Utf8BytesToString(PendingBytes.GetData(), PendingBytes.Num()).TrimStartAndEnd();
		if (Text.StartsWith(TEXT("{")) && Text.EndsWith(TEXT("}")))
		{
			TSharedPtr<FJsonObject> Probe;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			if (FJsonSerializer::Deserialize(Reader, Probe) && Probe.IsValid())
			{
				PendingBytes.Reset();
				HandleMessage(Text);
			}
		}
	}
}

void FMCPServerRunnable::HandleMessage(const FString& Message)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		SendResponse(ErrorJson(FString::Printf(TEXT("JSON inválido: %s"), *Message.Left(200))));
		return;
	}

	FString CommandType;
	if (!JsonObject->TryGetStringField(TEXT("type"), CommandType) && !JsonObject->TryGetStringField(TEXT("command"), CommandType))
	{
		SendResponse(ErrorJson(TEXT("Falta el campo 'type'")));
		return;
	}

	TSharedPtr<FJsonObject> Params;
	const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("params"), ParamsPtr) && ParamsPtr)
	{
		Params = *ParamsPtr;
	}
	else
	{
		Params = MakeShared<FJsonObject>();
	}

	SendResponse(Bridge->ExecuteCommand(CommandType, Params));
}

bool FMCPServerRunnable::SendResponse(const FString& Response)
{
	if (!ClientSocket)
	{
		return false;
	}

	FTCHARToUTF8 Utf8(*Response);
	TArray<uint8> Data;
	Data.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	Data.Add('\n');

	int32 SentTotal = 0;
	const double Deadline = FPlatformTime::Seconds() + 30.0;
	while (SentTotal < Data.Num())
	{
		int32 BytesSent = 0;
		if (ClientSocket->Send(Data.GetData() + SentTotal, Data.Num() - SentTotal, BytesSent) && BytesSent > 0)
		{
			SentTotal += BytesSent;
			continue;
		}

		const ESocketErrors Error = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
		if ((Error != SE_EWOULDBLOCK && Error != SE_NO_ERROR) || FPlatformTime::Seconds() > Deadline || !bRunning)
		{
			UE_LOG(LogUnrealMCP, Warning, TEXT("Falló el envío tras %d/%d bytes (error %d)"), SentTotal, Data.Num(), (int32)Error);
			return false;
		}
		ClientSocket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(20));
	}
	return true;
}
