#include "Commands/UnrealMCPLevelCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "LevelEditorViewport.h"
#include "FileHelpers.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "Components/SceneComponent.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "LevelEditor.h"
#include "IAssetViewport.h"

namespace
{
    /** Lee "actors" (array) o "actor"/"name" (string). */
    bool CollectActors(const TSharedPtr<FJsonObject>& Params, TArray<AActor*>& OutActors, FString& OutError)
    {
        TArray<FString> Names;
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (Params->TryGetArrayField(TEXT("actors"), Array))
        {
            for (const TSharedPtr<FJsonValue>& Value : *Array)
            {
                Names.Add(Value->AsString());
            }
        }
        FString Single;
        if (Params->TryGetStringField(TEXT("actor"), Single) || Params->TryGetStringField(TEXT("name"), Single))
        {
            Names.Add(Single);
        }
        if (Names.Num() == 0)
        {
            OutError = TEXT("Indica 'actor' o 'actors'");
            return false;
        }
        for (const FString& Name : Names)
        {
            AActor* Actor = FUnrealMCPCommonUtils::FindActor(Name, OutError);
            if (!Actor)
            {
                return false;
            }
            OutActors.AddUnique(Actor);
        }
        return true;
    }

    FLevelEditorViewportClient* GetLevelViewportClient()
    {
        if (GCurrentLevelEditingViewportClient)
        {
            return GCurrentLevelEditingViewportClient;
        }
        const TArray<FLevelEditorViewportClient*>& Clients = GEditor->GetLevelViewportClients();
        for (FLevelEditorViewportClient* Client : Clients)
        {
            if (Client && Client->IsPerspective())
            {
                return Client;
            }
        }
        return Clients.Num() > 0 ? Clients[0] : nullptr;
    }

    TArray<TSharedPtr<FJsonValue>> VectorToJson(const FVector& V)
    {
        return { MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z) };
    }

    TArray<TSharedPtr<FJsonValue>> RotatorToJson(const FRotator& R)
    {
        return { MakeShared<FJsonValueNumber>(R.Pitch), MakeShared<FJsonValueNumber>(R.Yaw), MakeShared<FJsonValueNumber>(R.Roll) };
    }

    TArray<TSharedPtr<FJsonValue>> ActorsToJson(const TArray<AActor*>& Actors)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (AActor* Actor : Actors)
        {
            Out.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
        }
        return Out;
    }
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("get_actor_components")) return HandleGetActorComponents(Params);
    if (CommandType == TEXT("get_selected_actors")) return HandleGetSelectedActors(Params);
    if (CommandType == TEXT("set_selection")) return HandleSetSelection(Params);
    if (CommandType == TEXT("set_actor_label")) return HandleSetActorLabel(Params);
    if (CommandType == TEXT("set_actor_folder")) return HandleSetActorFolder(Params);
    if (CommandType == TEXT("set_actor_tags")) return HandleSetActorTags(Params);
    if (CommandType == TEXT("duplicate_actor")) return HandleDuplicateActor(Params);
    if (CommandType == TEXT("attach_actor")) return HandleAttachActor(Params);
    if (CommandType == TEXT("get_level_info")) return HandleGetLevelInfo(Params);
    if (CommandType == TEXT("open_level")) return HandleOpenLevel(Params);
    if (CommandType == TEXT("save_level")) return HandleSaveLevel(Params);
    if (CommandType == TEXT("list_levels")) return HandleListLevels(Params);
    if (CommandType == TEXT("start_pie")) return HandleStartPIE(Params);
    if (CommandType == TEXT("stop_pie")) return HandleStopPIE(Params);
    if (CommandType == TEXT("is_pie_running")) return HandleIsPIERunning(Params);
    if (CommandType == TEXT("get_viewport_camera")) return HandleGetViewportCamera(Params);
    if (CommandType == TEXT("set_viewport_camera")) return HandleSetViewportCamera(Params);
    if (CommandType == TEXT("focus_actor")) return HandleFocusActor(Params);
    if (CommandType == TEXT("get_object_properties")) return HandleGetObjectProperties(Params);
    if (CommandType == TEXT("set_object_property")) return HandleSetObjectProperty(Params);

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Comando de nivel desconocido: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleGetActorComponents(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor"), ActorName) && !Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'actor'"));
    }
    bool bPIE = false;
    Params->TryGetBoolField(TEXT("pie"), bPIE);

    FString Error;
    AActor* Actor = FUnrealMCPCommonUtils::FindActor(ActorName, Error, FUnrealMCPCommonUtils::GetEditorWorld(bPIE));
    if (!Actor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    TArray<TSharedPtr<FJsonValue>> Components;
    TInlineComponentArray<UActorComponent*> ActorComponents(Actor);
    for (UActorComponent* Component : ActorComponents)
    {
        if (!Component)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("name"), Component->GetName());
        Entry->SetStringField(TEXT("class"), Component->GetClass()->GetName());
        Entry->SetBoolField(TEXT("active"), Component->IsActive());
        if (USceneComponent* Scene = Cast<USceneComponent>(Component))
        {
            Entry->SetBoolField(TEXT("is_root"), Scene == Actor->GetRootComponent());
            if (USceneComponent* Parent = Scene->GetAttachParent())
            {
                Entry->SetStringField(TEXT("attach_parent"), Parent->GetName());
            }
            Entry->SetArrayField(TEXT("relative_location"), VectorToJson(Scene->GetRelativeLocation()));
            Entry->SetArrayField(TEXT("relative_rotation"), RotatorToJson(Scene->GetRelativeRotation()));
            Entry->SetArrayField(TEXT("relative_scale"), VectorToJson(Scene->GetRelativeScale3D()));
            Entry->SetStringField(TEXT("mobility"), StaticEnum<EComponentMobility::Type>()->GetNameStringByValue(Scene->Mobility));
            Entry->SetBoolField(TEXT("visible"), Scene->IsVisible());
        }
        Components.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("actor"), Actor->GetActorLabel());
    Result->SetArrayField(TEXT("components"), Components);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleGetSelectedActors(const TSharedPtr<FJsonObject>& Params)
{
    TArray<AActor*> Selected;
    GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(Selected);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("actors"), ActorsToJson(Selected));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleSetSelection(const TSharedPtr<FJsonObject>& Params)
{
    bool bAdd = false;
    Params->TryGetBoolField(TEXT("add"), bAdd);

    TArray<AActor*> Actors;
    FString Error;
    const TArray<TSharedPtr<FJsonValue>>* ActorArray = nullptr;
    const bool bHasActors = (Params->TryGetArrayField(TEXT("actors"), ActorArray) && ActorArray->Num() > 0) || Params->HasField(TEXT("actor"));
    if (bHasActors && !CollectActors(Params, Actors, Error))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    if (!bAdd)
    {
        GEditor->SelectNone(false, true, false);
    }
    for (AActor* Actor : Actors)
    {
        GEditor->SelectActor(Actor, true, false, true);
    }
    GEditor->NoteSelectionChange();

    return HandleGetSelectedActors(Params);
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleSetActorLabel(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName, Label;
    if (!Params->TryGetStringField(TEXT("actor"), ActorName) || !Params->TryGetStringField(TEXT("label"), Label))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Faltan 'actor' y 'label'"));
    }

    FString Error;
    AActor* Actor = FUnrealMCPCommonUtils::FindActor(ActorName, Error);
    if (!Actor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Rename Actor"));
    Actor->SetActorLabel(Label, true);
    return FUnrealMCPCommonUtils::ActorToJsonObject(Actor);
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleSetActorFolder(const TSharedPtr<FJsonObject>& Params)
{
    FString Folder;
    Params->TryGetStringField(TEXT("folder"), Folder);

    TArray<AActor*> Actors;
    FString Error;
    if (!CollectActors(Params, Actors, Error))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Set Actor Folder"));
    for (AActor* Actor : Actors)
    {
        Actor->Modify();
        Actor->SetFolderPath(Folder.IsEmpty() ? NAME_None : FName(*Folder));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("actors"), ActorsToJson(Actors));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleSetActorTags(const TSharedPtr<FJsonObject>& Params)
{
    FString Mode = TEXT("set");
    Params->TryGetStringField(TEXT("mode"), Mode);

    TArray<FName> Tags;
    const TArray<TSharedPtr<FJsonValue>>* TagArray = nullptr;
    if (Params->TryGetArrayField(TEXT("tags"), TagArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *TagArray)
        {
            Tags.Add(FName(*Value->AsString()));
        }
    }

    TArray<AActor*> Actors;
    FString Error;
    if (!CollectActors(Params, Actors, Error))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Set Actor Tags"));
    for (AActor* Actor : Actors)
    {
        Actor->Modify();
        if (Mode == TEXT("add"))
        {
            for (const FName& Tag : Tags)
            {
                Actor->Tags.AddUnique(Tag);
            }
        }
        else if (Mode == TEXT("remove"))
        {
            for (const FName& Tag : Tags)
            {
                Actor->Tags.Remove(Tag);
            }
        }
        else
        {
            Actor->Tags = Tags;
        }
    }

    TArray<TSharedPtr<FJsonValue>> Out;
    for (AActor* Actor : Actors)
    {
        Out.Add(MakeShared<FJsonValueObject>(FUnrealMCPCommonUtils::ActorToJsonObject(Actor, true)));
    }
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("actors"), Out);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleDuplicateActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'actor'"));
    }

    FString Error;
    AActor* Source = FUnrealMCPCommonUtils::FindActor(ActorName, Error);
    if (!Source)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    const FVector Offset = Params->HasField(TEXT("offset")) ? FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("offset")) : FVector::ZeroVector;

    UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Duplicate Actor"));
    AActor* Copy = ActorSubsystem ? ActorSubsystem->DuplicateActor(Source, nullptr, Offset) : nullptr;
    if (!Copy)
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se pudo duplicar el actor"));
    }

    FString NewLabel;
    if (Params->TryGetStringField(TEXT("new_label"), NewLabel) && !NewLabel.IsEmpty())
    {
        Copy->SetActorLabel(NewLabel);
    }
    return FUnrealMCPCommonUtils::ActorToJsonObject(Copy, true);
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleAttachActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName, ParentName, Socket;
    if (!Params->TryGetStringField(TEXT("actor"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'actor'"));
    }
    Params->TryGetStringField(TEXT("parent"), ParentName);
    Params->TryGetStringField(TEXT("socket"), Socket);

    FString Error;
    AActor* Child = FUnrealMCPCommonUtils::FindActor(ActorName, Error);
    if (!Child)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Attach Actor"));
    Child->Modify();
    if (ParentName.IsEmpty())
    {
        Child->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
    }
    else
    {
        AActor* Parent = FUnrealMCPCommonUtils::FindActor(ParentName, Error);
        if (!Parent)
        {
            Transaction.Cancel();
            return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
        }
        Parent->Modify();
        GEditor->ParentActors(Parent, Child, Socket.IsEmpty() ? NAME_None : FName(*Socket));
    }
    return FUnrealMCPCommonUtils::ActorToJsonObject(Child, true);
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleGetLevelInfo(const TSharedPtr<FJsonObject>& Params)
{
    UWorld* World = FUnrealMCPCommonUtils::GetEditorWorld();
    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No hay mundo del editor"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("map_name"), World->GetMapName());
    Result->SetStringField(TEXT("package"), World->GetOutermost()->GetName());
    Result->SetBoolField(TEXT("dirty"), World->GetOutermost()->IsDirty());
    Result->SetBoolField(TEXT("world_partition"), World->IsPartitionedWorld());
    Result->SetBoolField(TEXT("pie_running"), GEditor->PlayWorld != nullptr);

    int32 ActorCount = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        ++ActorCount;
    }
    Result->SetNumberField(TEXT("actor_count"), ActorCount);

    if (AWorldSettings* Settings = World->GetWorldSettings())
    {
        if (Settings->DefaultGameMode)
        {
            Result->SetStringField(TEXT("game_mode_override"), Settings->DefaultGameMode->GetPathName());
        }
    }

    TArray<TSharedPtr<FJsonValue>> Streaming;
    for (ULevelStreaming* Level : World->GetStreamingLevels())
    {
        if (!Level)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("package"), Level->GetWorldAssetPackageName());
        Entry->SetBoolField(TEXT("loaded"), Level->IsLevelLoaded());
        Entry->SetBoolField(TEXT("visible"), Level->IsLevelVisible());
        Streaming.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetArrayField(TEXT("streaming_levels"), Streaming);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleOpenLevel(const TSharedPtr<FJsonObject>& Params)
{
    FString LevelPath;
    if (!Params->TryGetStringField(TEXT("path"), LevelPath) || LevelPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'path' (p. ej. /Game/Maps/MiMapa)"));
    }
    if (GEditor->PlayWorld)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Detén PIE antes de abrir otro nivel"));
    }

    bool bForce = false;
    Params->TryGetBoolField(TEXT("discard_changes"), bForce);
    UWorld* Current = FUnrealMCPCommonUtils::GetEditorWorld();
    if (!bForce && Current && Current->GetOutermost()->IsDirty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("El nivel actual tiene cambios sin guardar. Usa save_level o pasa discard_changes=true"));
    }

    LevelPath.Split(TEXT("."), &LevelPath, nullptr);
    if (!FPackageName::DoesPackageExist(LevelPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No existe el nivel %s"), *LevelPath));
    }

    if (bForce && Current)
    {
        Current->GetOutermost()->SetDirtyFlag(false);
    }
    UWorld* Loaded = UEditorLoadingAndSavingUtils::LoadMap(LevelPath);
    if (!Loaded)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No se pudo abrir %s"), *LevelPath));
    }
    return HandleGetLevelInfo(Params);
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleSaveLevel(const TSharedPtr<FJsonObject>& Params)
{
    if (GEditor->PlayWorld)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Detén PIE antes de guardar"));
    }

    bool bAllDirty = false;
    Params->TryGetBoolField(TEXT("all_dirty"), bAllDirty);

    bool bOk = false;
    if (bAllDirty)
    {
        bOk = UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, true);
    }
    else
    {
        bOk = UEditorLoadingAndSavingUtils::SaveCurrentLevel();
    }
    if (!bOk)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se pudo guardar (¿nivel sin guardar nunca, o archivo de solo lectura?)"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("saved"), true);
    Result->SetStringField(TEXT("level"), FUnrealMCPCommonUtils::GetEditorWorld()->GetOutermost()->GetName());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleListLevels(const TSharedPtr<FJsonObject>& Params)
{
    FString Path = TEXT("/Game");
    Params->TryGetStringField(TEXT("path"), Path);

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    FARFilter Filter;
    Filter.bRecursivePaths = true;
    Filter.PackagePaths.Add(FName(*Path));
    Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
    TArray<FAssetData> Assets;
    Registry.GetAssets(Filter, Assets);

    TArray<FString> Levels;
    for (const FAssetData& Asset : Assets)
    {
        Levels.Add(Asset.PackageName.ToString());
    }
    Levels.Sort();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("levels"), FUnrealMCPCommonUtils::StringArrayToJson(Levels));
    Result->SetStringField(TEXT("current"), FUnrealMCPCommonUtils::GetEditorWorld()->GetOutermost()->GetName());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleStartPIE(const TSharedPtr<FJsonObject>& Params)
{
    if (GEditor->PlayWorld || GEditor->IsPlaySessionInProgress())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PIE ya está corriendo"));
    }

    FString Mode = TEXT("viewport");
    Params->TryGetStringField(TEXT("mode"), Mode);

    FRequestPlaySessionParams SessionParams;
    if (Mode == TEXT("simulate"))
    {
        SessionParams.WorldType = EPlaySessionWorldType::SimulateInEditor;
    }
    else if (Mode == TEXT("vr"))
    {
        SessionParams.SessionPreviewTypeOverride = EPlaySessionPreviewType::VRPreview;
    }
    if (Mode == TEXT("viewport") || Mode == TEXT("simulate"))
    {
        // Sin destino, PIE abre una ventana nueva (modo "window").
        FLevelEditorModule& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
        SessionParams.DestinationSlateViewport = LevelEditor.GetFirstActiveViewport();
    }

    GEditor->RequestPlaySession(SessionParams);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("requested_mode"), Mode);
    Result->SetStringField(TEXT("note"), TEXT("PIE arranca en el próximo tick; usa is_pie_running para confirmar"));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleStopPIE(const TSharedPtr<FJsonObject>& Params)
{
    if (!GEditor->PlayWorld && !GEditor->IsPlaySessionInProgress())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PIE no está corriendo"));
    }
    GEditor->RequestEndPlayMap();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("stop_requested"), true);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleIsPIERunning(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("running"), GEditor->PlayWorld != nullptr);
    Result->SetBoolField(TEXT("requested"), GEditor->IsPlaySessionInProgress());
    if (GEditor->PlayWorld)
    {
        Result->SetStringField(TEXT("world"), GEditor->PlayWorld->GetMapName());
        Result->SetBoolField(TEXT("simulating"), GEditor->bIsSimulatingInEditor);
    }
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleGetViewportCamera(const TSharedPtr<FJsonObject>& Params)
{
    FLevelEditorViewportClient* Client = GetLevelViewportClient();
    if (!Client)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No hay viewport de nivel"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("location"), VectorToJson(Client->GetViewLocation()));
    Result->SetArrayField(TEXT("rotation"), RotatorToJson(Client->GetViewRotation()));
    Result->SetNumberField(TEXT("fov"), Client->ViewFOV);
    Result->SetBoolField(TEXT("perspective"), Client->IsPerspective());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleSetViewportCamera(const TSharedPtr<FJsonObject>& Params)
{
    FLevelEditorViewportClient* Client = GetLevelViewportClient();
    if (!Client)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No hay viewport de nivel"));
    }

    if (Params->HasField(TEXT("location")))
    {
        Client->SetViewLocation(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Client->SetViewRotation(FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
    }
    double Fov = 0.0;
    if (Params->TryGetNumberField(TEXT("fov"), Fov) && Fov > 0.0)
    {
        Client->ViewFOV = static_cast<float>(Fov);
    }
    Client->Invalidate();
    return HandleGetViewportCamera(Params);
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleFocusActor(const TSharedPtr<FJsonObject>& Params)
{
    TArray<AActor*> Actors;
    FString Error;
    if (!CollectActors(Params, Actors, Error))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    bool bSelect = true;
    Params->TryGetBoolField(TEXT("select"), bSelect);
    if (bSelect)
    {
        GEditor->SelectNone(false, true, false);
        for (AActor* Actor : Actors)
        {
            GEditor->SelectActor(Actor, true, false, true);
        }
        GEditor->NoteSelectionChange();
    }

    FBox Bounds(ForceInit);
    for (AActor* Actor : Actors)
    {
        Bounds += Actor->GetComponentsBoundingBox(true);
    }
    if (FLevelEditorViewportClient* Client = GetLevelViewportClient())
    {
        Client->FocusViewportOnBox(Bounds, true);
    }
    return HandleGetViewportCamera(Params);
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleGetObjectProperties(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    UObject* Target = FUnrealMCPCommonUtils::ResolveTargetObject(Params, Error);
    if (!Target)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), Target->GetPathName());
    Result->SetStringField(TEXT("class"), Target->GetClass()->GetPathName());

    // Una propiedad concreta (con ruta) o la lista completa.
    FString PropertyPath;
    if (Params->TryGetStringField(TEXT("property"), PropertyPath) && !PropertyPath.IsEmpty())
    {
        FProperty* Property = nullptr;
        void* ValuePtr = nullptr;
        UObject* Owner = nullptr;
        if (!FUnrealMCPCommonUtils::ResolvePropertyPath(Target, PropertyPath, Property, ValuePtr, Owner, Error))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
        }
        Result->SetStringField(TEXT("property"), PropertyPath);
        Result->SetStringField(TEXT("type"), Property->GetCPPType());
        Result->SetField(TEXT("value"), FUnrealMCPCommonUtils::PropertyValueToJson(Property, ValuePtr));
        return Result;
    }

    FString Filter;
    Params->TryGetStringField(TEXT("filter"), Filter);
    bool bAll = false;
    Params->TryGetBoolField(TEXT("include_all"), bAll);
    Result->SetObjectField(TEXT("properties"), FUnrealMCPCommonUtils::ObjectPropertiesToJson(Target, Filter, !bAll));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPLevelCommands::HandleSetObjectProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    UObject* Target = FUnrealMCPCommonUtils::ResolveTargetObject(Params, Error);
    if (!Target)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    // "properties": {"Ruta": valor, ...} o "property" + "value".
    TArray<TPair<FString, TSharedPtr<FJsonValue>>> Assignments;
    const TSharedPtr<FJsonObject>* Batch = nullptr;
    if (Params->TryGetObjectField(TEXT("properties"), Batch) && Batch)
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Batch)->Values)
        {
            Assignments.Add(Pair);
        }
    }
    FString PropertyPath;
    if (Params->TryGetStringField(TEXT("property"), PropertyPath) && Params->HasField(TEXT("value")))
    {
        Assignments.Add({ PropertyPath, Params->TryGetField(TEXT("value")) });
    }
    if (Assignments.Num() == 0)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Indica 'property' + 'value' o 'properties': {ruta: valor}"));
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Set Properties"));
    TSharedPtr<FJsonObject> Applied = MakeShared<FJsonObject>();
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Assignment : Assignments)
    {
        if (!FUnrealMCPCommonUtils::SetObjectPropertyByPath(Target, Assignment.Key, Assignment.Value, Error))
        {
            Transaction.Cancel();
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("%s: %s (transacción cancelada; revisa con get_object_properties)"), *Assignment.Key, *Error));
        }

        FProperty* Property = nullptr;
        void* ValuePtr = nullptr;
        UObject* Owner = nullptr;
        if (FUnrealMCPCommonUtils::ResolvePropertyPath(Target, Assignment.Key, Property, ValuePtr, Owner, Error))
        {
            Applied->SetField(Assignment.Key, FUnrealMCPCommonUtils::PropertyValueToJson(Property, ValuePtr));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), Target->GetPathName());
    Result->SetObjectField(TEXT("applied"), Applied);
    return Result;
}
