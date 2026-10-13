#include "UnrealMCPBridge.h"
#include "UnrealMCPSettings.h"
#include "MCPServerRunnable.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/RunnableThread.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Commands/UnrealMCPEditorCommands.h"
#include "Commands/UnrealMCPLevelCommands.h"
#include "Commands/UnrealMCPScriptCommands.h"
#include "Commands/UnrealMCPMaterialCommands.h"
#include "Commands/UnrealMCPBlueprintCommands.h"
#include "Commands/UnrealMCPBlueprintNodeCommands.h"
#include "Commands/UnrealMCPBlueprintReadCommands.h"
#include "Commands/UnrealMCPProjectCommands.h"
#include "Commands/UnrealMCPInputCommands.h"
#include "Commands/UnrealMCPAssetCommands.h"
#include "Commands/UnrealMCPGraphEditCommands.h"
#include "Commands/UnrealMCPUMGCommands.h"

DEFINE_LOG_CATEGORY(LogUnrealMCP);

namespace
{
	FString SerializeJson(const TSharedPtr<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Out;
	}

	/** Estado compartido entre el hilo del socket y el game thread. */
	struct FPendingCommand
	{
		FCriticalSection Lock;
		bool bStarted = false;
		bool bCancelled = false;
		FString Result;
		FEvent* DoneEvent = nullptr;

		FPendingCommand() { DoneEvent = FPlatformProcess::GetSynchEventFromPool(true); }
		~FPendingCommand() { FPlatformProcess::ReturnSynchEventToPool(DoneEvent); }
	};

	/** Crea el handler y lo guarda en Owners para que viva tanto como el bridge. */
	template <typename T>
	FUnrealMCPCommandHandler MakeHandler(TArray<TSharedPtr<void>>& Owners)
	{
		TSharedPtr<T> Instance = MakeShared<T>();
		Owners.Add(Instance);
		return [Instance](const FString& Command, const TSharedPtr<FJsonObject>& Params)
		{
			return Instance->HandleCommand(Command, Params);
		};
	}
}

UUnrealMCPBridge::UUnrealMCPBridge()
{
}

UUnrealMCPBridge::~UUnrealMCPBridge()
{
}

void UUnrealMCPBridge::Initialize(FSubsystemCollectionBase& Collection)
{
	const UUnrealMCPSettings* Settings = GetDefault<UUnrealMCPSettings>();
	Port = static_cast<uint16>(Settings->Port);
	if (!FIPv4Address::Parse(Settings->Host, ServerAddress))
	{
		UE_LOG(LogUnrealMCP, Warning, TEXT("Host inválido '%s', se usa 127.0.0.1"), *Settings->Host);
		ServerAddress = FIPv4Address(127, 0, 0, 1);
	}

	RegisterAllCommands();

	// En commandlets (cook, build...) nadie usa el servidor, y otro editor abierto ya puede tener el puerto
	if (Settings->bAutoStart && !IsRunningCommandlet())
	{
		StartServer();
	}
}

void UUnrealMCPBridge::Deinitialize()
{
	StopServer();
	CommandRegistry.Empty();
	HandlerOwners.Empty();
}

void UUnrealMCPBridge::RegisterCommands(const FString& Toolset, const TArray<FString>& Commands, FUnrealMCPCommandHandler Handler)
{
	for (const FString& Command : Commands)
	{
		CommandRegistry.Add(Command, FCommandEntry{ Toolset, Handler });
	}
}

void UUnrealMCPBridge::RegisterAllCommands()
{
	RegisterCommands(TEXT("editor"), {
		TEXT("get_actors_in_level"), TEXT("find_actors_by_name"), TEXT("spawn_actor"), TEXT("create_actor"),
		TEXT("delete_actor"), TEXT("set_actor_transform"), TEXT("get_actor_properties"), TEXT("set_actor_property"),
		TEXT("spawn_blueprint_actor"), TEXT("focus_viewport"), TEXT("take_screenshot")
	}, MakeHandler<FUnrealMCPEditorCommands>(HandlerOwners));

	RegisterCommands(TEXT("level"), {
		TEXT("get_actor_components"), TEXT("get_selected_actors"), TEXT("set_selection"), TEXT("set_actor_label"),
		TEXT("set_actor_folder"), TEXT("set_actor_tags"), TEXT("duplicate_actor"), TEXT("attach_actor"),
		TEXT("get_level_info"), TEXT("open_level"), TEXT("save_level"), TEXT("list_levels"),
		TEXT("start_pie"), TEXT("stop_pie"), TEXT("is_pie_running"),
		TEXT("get_viewport_camera"), TEXT("set_viewport_camera"), TEXT("focus_actor"),
		TEXT("get_object_properties"), TEXT("set_object_property")
	}, MakeHandler<FUnrealMCPLevelCommands>(HandlerOwners));

	RegisterCommands(TEXT("script"), {
		TEXT("execute_python"), TEXT("console_command")
	}, MakeHandler<FUnrealMCPScriptCommands>(HandlerOwners));

	RegisterCommands(TEXT("material"), {
		TEXT("create_material_instance"), TEXT("set_material_instance_param"), TEXT("get_material_params"),
		TEXT("set_actor_material")
	}, MakeHandler<FUnrealMCPMaterialCommands>(HandlerOwners));

	RegisterCommands(TEXT("asset"), {
		TEXT("duplicate_asset"), TEXT("save_assets"), TEXT("find_asset_references"), TEXT("get_editor_log"),
		TEXT("search_assets"), TEXT("get_asset_info"), TEXT("create_asset"), TEXT("rename_asset"),
		TEXT("delete_asset"), TEXT("create_folder"), TEXT("import_asset")
	}, MakeHandler<FUnrealMCPAssetCommands>(HandlerOwners));

	RegisterCommands(TEXT("blueprint"), {
		TEXT("create_blueprint"), TEXT("add_component_to_blueprint"), TEXT("set_component_property"),
		TEXT("set_physics_properties"), TEXT("compile_blueprint"), TEXT("set_blueprint_property"),
		TEXT("set_static_mesh_properties"), TEXT("set_pawn_properties"), TEXT("dump_blueprint_graph")
	}, MakeHandler<FUnrealMCPBlueprintCommands>(HandlerOwners));

	RegisterCommands(TEXT("blueprint_read"), {
		TEXT("list_blueprints"), TEXT("get_blueprint_overview"), TEXT("read_blueprint_graph"),
		TEXT("find_in_blueprints"), TEXT("export_blueprint_nodes_text"), TEXT("read_data_asset")
	}, MakeHandler<FUnrealMCPBlueprintReadCommands>(HandlerOwners));

	RegisterCommands(TEXT("blueprint_node"), {
		TEXT("connect_blueprint_nodes"), TEXT("add_blueprint_get_self_component_reference"),
		TEXT("add_blueprint_self_reference"), TEXT("find_blueprint_nodes"), TEXT("add_blueprint_event_node"),
		TEXT("add_blueprint_input_action_node"), TEXT("add_blueprint_function_node"), TEXT("add_blueprint_variable")
	}, MakeHandler<FUnrealMCPBlueprintNodeCommands>(HandlerOwners));

	RegisterCommands(TEXT("graph_edit"), {
		TEXT("add_blueprint_function"), TEXT("add_event_dispatcher"), TEXT("add_custom_event"),
		TEXT("add_blueprint_node"), TEXT("connect_blueprint_pins"), TEXT("disconnect_blueprint_pins"),
		TEXT("set_pin_default"), TEXT("delete_blueprint_node"), TEXT("apply_blueprint_graph"),
		TEXT("search_callable_functions")
	}, MakeHandler<FUnrealMCPGraphEditCommands>(HandlerOwners));

	RegisterCommands(TEXT("input"), {
		TEXT("read_input_asset"), TEXT("find_input_bindings"), TEXT("add_enhanced_input_mapping"),
		TEXT("remove_enhanced_input_mapping")
	}, MakeHandler<FUnrealMCPInputCommands>(HandlerOwners));

	RegisterCommands(TEXT("project"), {
		TEXT("create_input_mapping")
	}, MakeHandler<FUnrealMCPProjectCommands>(HandlerOwners));

	RegisterCommands(TEXT("umg"), {
		TEXT("create_umg_widget_blueprint"), TEXT("add_text_block_to_widget"), TEXT("add_button_to_widget"),
		TEXT("bind_widget_event"), TEXT("set_text_block_binding"), TEXT("add_widget_to_viewport"),
		TEXT("add_widget"), TEXT("get_widget_tree"), TEXT("set_widget_property")
	}, MakeHandler<FUnrealMCPUMGCommands>(HandlerOwners));
}

void UUnrealMCPBridge::StartServer()
{
	if (bIsRunning)
	{
		UE_LOG(LogUnrealMCP, Warning, TEXT("El servidor ya está corriendo"));
		return;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		UE_LOG(LogUnrealMCP, Error, TEXT("No hay socket subsystem"));
		return;
	}

	// El socket se destruye con el subsystem de sockets cuando se suelta la última referencia (bridge e hilo);
	// destruirlo a mano además del TSharedPtr lo libera dos veces
	TSharedPtr<FSocket> NewListenerSocket(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UnrealMCPListener"), false),
		[](FSocket* Socket)
		{
			if (ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
			{
				Sockets->DestroySocket(Socket);
			}
		});
	if (!NewListenerSocket.IsValid())
	{
		UE_LOG(LogUnrealMCP, Error, TEXT("No se pudo crear el socket de escucha"));
		return;
	}

	NewListenerSocket->SetReuseAddr(true);
	NewListenerSocket->SetNonBlocking(true);

	FIPv4Endpoint Endpoint(ServerAddress, Port);
	if (!NewListenerSocket->Bind(*Endpoint.ToInternetAddr()) || !NewListenerSocket->Listen(8))
	{
		UE_LOG(LogUnrealMCP, Error, TEXT("No se pudo escuchar en %s:%d (¿puerto ocupado?)"), *ServerAddress.ToString(), Port);
		return;
	}

	ListenerSocket = NewListenerSocket;
	ServerRunnable = new FMCPServerRunnable(this, ListenerSocket);
	ServerThread = FRunnableThread::Create(ServerRunnable, TEXT("UnrealMCPServerThread"), 0, TPri_Normal);
	if (!ServerThread)
	{
		UE_LOG(LogUnrealMCP, Error, TEXT("No se pudo crear el hilo del servidor"));
		delete ServerRunnable;
		ServerRunnable = nullptr;
		ListenerSocket.Reset();
		return;
	}

	bIsRunning = true;
	UE_LOG(LogUnrealMCP, Display, TEXT("Servidor MCP escuchando en %s:%d (%d comandos)"), *ServerAddress.ToString(), Port, CommandRegistry.Num());
}

void UUnrealMCPBridge::StopServer()
{
	if (!bIsRunning)
	{
		return;
	}
	bIsRunning = false;

	if (ServerThread)
	{
		// Stop() pide al bucle que termine; Kill(true) espera a que salga.
		ServerThread->Kill(true);
		delete ServerThread;
		ServerThread = nullptr;
	}
	delete ServerRunnable;
	ServerRunnable = nullptr;

	if (ListenerSocket.IsValid())
	{
		ListenerSocket->Close();
		ListenerSocket.Reset();
	}

	UE_LOG(LogUnrealMCP, Display, TEXT("Servidor MCP detenido"));
}

FString UUnrealMCPBridge::MakeErrorResponse(const FString& Message)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("status"), TEXT("error"));
	Response->SetStringField(TEXT("error"), Message);
	return SerializeJson(Response);
}

TSharedPtr<FJsonObject> UUnrealMCPBridge::HandleListCommands() const
{
	TMap<FString, TArray<FString>> ByToolset;
	for (const TPair<FString, FCommandEntry>& Pair : CommandRegistry)
	{
		ByToolset.FindOrAdd(Pair.Value.Toolset).Add(Pair.Key);
	}

	TSharedPtr<FJsonObject> Toolsets = MakeShared<FJsonObject>();
	for (TPair<FString, TArray<FString>>& Pair : ByToolset)
	{
		Pair.Value.Sort();
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& Name : Pair.Value)
		{
			Values.Add(MakeShared<FJsonValueString>(Name));
		}
		Toolsets->SetArrayField(Pair.Key, Values);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("toolsets"), Toolsets);
	Result->SetNumberField(TEXT("count"), CommandRegistry.Num());
	return Result;
}

TSharedPtr<FJsonObject> UUnrealMCPBridge::DispatchOnGameThread(const FString& CommandType, const TSharedPtr<FJsonObject>& InParams)
{
	check(IsInGameThread());

	TSharedPtr<FJsonObject> Params = InParams.IsValid() ? InParams : MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> Result;
	if (CommandType == TEXT("ping"))
	{
		Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("message"), TEXT("pong"));
	}
	else if (CommandType == TEXT("list_commands"))
	{
		Result = HandleListCommands();
	}
	else if (const FCommandEntry* Entry = CommandRegistry.Find(CommandType))
	{
		Result = Entry->Handler(CommandType, Params);
	}
	else
	{
		Response->SetStringField(TEXT("status"), TEXT("error"));
		Response->SetStringField(TEXT("error"), FString::Printf(TEXT("Comando desconocido: %s. Usa list_commands."), *CommandType));
		return Response;
	}

	if (!Result.IsValid())
	{
		Response->SetStringField(TEXT("status"), TEXT("error"));
		Response->SetStringField(TEXT("error"), FString::Printf(TEXT("El comando %s no devolvió resultado"), *CommandType));
		return Response;
	}

	bool bSuccess = true;
	Result->TryGetBoolField(TEXT("success"), bSuccess);
	if (bSuccess)
	{
		Response->SetStringField(TEXT("status"), TEXT("success"));
		Response->SetObjectField(TEXT("result"), Result);
	}
	else
	{
		FString Error;
		Result->TryGetStringField(TEXT("error"), Error);
		Response->SetStringField(TEXT("status"), TEXT("error"));
		Response->SetStringField(TEXT("error"), Error.IsEmpty() ? TEXT("Error sin mensaje") : Error);
	}
	return Response;
}

FString UUnrealMCPBridge::ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (GetDefault<UUnrealMCPSettings>()->bVerboseLogging)
	{
		UE_LOG(LogUnrealMCP, Display, TEXT("Comando: %s"), *CommandType);
	}

	if (IsInGameThread())
	{
		return SerializeJson(DispatchOnGameThread(CommandType, Params));
	}

	TSharedRef<FPendingCommand, ESPMode::ThreadSafe> Pending = MakeShared<FPendingCommand, ESPMode::ThreadSafe>();
	TWeakObjectPtr<UUnrealMCPBridge> WeakThis(this);

	AsyncTask(ENamedThreads::GameThread, [WeakThis, CommandType, Params, Pending]()
	{
		{
			FScopeLock Lock(&Pending->Lock);
			if (Pending->bCancelled)
			{
				return;
			}
			Pending->bStarted = true;
		}

		FString Result;
		if (UUnrealMCPBridge* Bridge = WeakThis.Get())
		{
			Result = SerializeJson(Bridge->DispatchOnGameThread(CommandType, Params));
		}
		else
		{
			Result = MakeErrorResponse(TEXT("El bridge MCP ya no existe"));
		}

		FScopeLock Lock(&Pending->Lock);
		Pending->Result = MoveTemp(Result);
		Pending->DoneEvent->Trigger();
	});

	const float TimeoutSeconds = GetDefault<UUnrealMCPSettings>()->CommandTimeoutSeconds;
	if (Pending->DoneEvent->Wait(FTimespan::FromSeconds(TimeoutSeconds)))
	{
		FScopeLock Lock(&Pending->Lock);
		return Pending->Result;
	}

	FScopeLock Lock(&Pending->Lock);
	if (!Pending->bStarted)
	{
		Pending->bCancelled = true;
		return MakeErrorResponse(FString::Printf(
			TEXT("Timeout (%.0fs): el game thread está ocupado (¿diálogo modal o compilación?). El comando '%s' NO se ejecutó."),
			TimeoutSeconds, *CommandType));
	}
	return MakeErrorResponse(FString::Printf(
		TEXT("Timeout (%.0fs): el comando '%s' sigue corriendo y sus cambios se aplicarán al terminar."),
		TimeoutSeconds, *CommandType));
}
