#include "Commands/UnrealMCPEditorCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "GameFramework/Actor.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "RenderingThread.h"
#include "HAL/FileManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"

namespace
{
    FTransform ReadTransform(const TSharedPtr<FJsonObject>& Params, const FTransform& Base = FTransform::Identity)
    {
        FTransform Transform = Base;
        if (Params->HasField(TEXT("location")))
        {
            Transform.SetLocation(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
        }
        if (Params->HasField(TEXT("rotation")))
        {
            Transform.SetRotation(FQuat(FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"))));
        }
        if (Params->HasField(TEXT("scale")))
        {
            Transform.SetScale3D(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
        }
        return Transform;
    }

    AActor* SpawnInEditor(UClass* Class, const FTransform& Transform, const FString& Label, FString& OutError)
    {
        UWorld* World = FUnrealMCPCommonUtils::GetEditorWorld();
        if (!World)
        {
            OutError = TEXT("No hay mundo del editor");
            return nullptr;
        }
        if (!Class || !Class->IsChildOf(AActor::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract))
        {
            OutError = FString::Printf(TEXT("%s no es una clase de actor instanciable"), Class ? *Class->GetName() : TEXT("null"));
            return nullptr;
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AActor* Actor = World->SpawnActor<AActor>(Class, Transform, SpawnParams);
        if (!Actor)
        {
            OutError = TEXT("SpawnActor falló");
            return nullptr;
        }
        // SpawnActor ignora la escala cuando el actor tiene componentes con escala propia.
        Actor->SetActorScale3D(Transform.GetScale3D());
        if (!Label.IsEmpty())
        {
            Actor->SetActorLabel(Label);
        }
        return Actor;
    }
}

FUnrealMCPEditorCommands::FUnrealMCPEditorCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("get_actors_in_level"))
    {
        return HandleGetActorsInLevel(Params);
    }
    if (CommandType == TEXT("find_actors_by_name"))
    {
        return HandleFindActorsByName(Params);
    }
    if (CommandType == TEXT("spawn_actor") || CommandType == TEXT("create_actor"))
    {
        return HandleSpawnActor(Params);
    }
    if (CommandType == TEXT("delete_actor"))
    {
        return HandleDeleteActor(Params);
    }
    if (CommandType == TEXT("set_actor_transform"))
    {
        return HandleSetActorTransform(Params);
    }
    if (CommandType == TEXT("get_actor_properties"))
    {
        return HandleGetActorProperties(Params);
    }
    if (CommandType == TEXT("set_actor_property"))
    {
        return HandleSetActorProperty(Params);
    }
    if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    if (CommandType == TEXT("focus_viewport"))
    {
        return HandleFocusViewport(Params);
    }
    if (CommandType == TEXT("take_screenshot"))
    {
        return HandleTakeScreenshot(Params);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Comando de editor desconocido: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params)
{
    bool bPIE = false;
    Params->TryGetBoolField(TEXT("pie"), bPIE);
    FString ClassFilter;
    Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
    FString FolderFilter;
    Params->TryGetStringField(TEXT("folder"), FolderFilter);
    int32 Limit = 500;
    Params->TryGetNumberField(TEXT("limit"), Limit);

    UWorld* World = FUnrealMCPCommonUtils::GetEditorWorld(bPIE);
    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No hay mundo del editor"));
    }

    TArray<TSharedPtr<FJsonValue>> ActorArray;
    int32 Total = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (!IsValid(Actor))
        {
            continue;
        }
        if (!ClassFilter.IsEmpty() && !Actor->GetClass()->GetName().Contains(ClassFilter))
        {
            continue;
        }
        if (!FolderFilter.IsEmpty() && !Actor->GetFolderPath().ToString().StartsWith(FolderFilter))
        {
            continue;
        }
        ++Total;
        if (ActorArray.Num() < Limit)
        {
            ActorArray.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("level"), World->GetMapName());
    ResultObj->SetNumberField(TEXT("total"), Total);
    ResultObj->SetArrayField(TEXT("actors"), ActorArray);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params)
{
    FString Pattern;
    if (!Params->TryGetStringField(TEXT("pattern"), Pattern))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'pattern'"));
    }
    bool bPIE = false;
    Params->TryGetBoolField(TEXT("pie"), bPIE);

    UWorld* World = FUnrealMCPCommonUtils::GetEditorWorld(bPIE);
    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No hay mundo del editor"));
    }

    TArray<TSharedPtr<FJsonValue>> MatchingActors;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Actor = *It;
        if (IsValid(Actor) && (Actor->GetName().Contains(Pattern) || Actor->GetActorLabel().Contains(Pattern)))
        {
            MatchingActors.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), MatchingActors);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorType;
    if (!Params->TryGetStringField(TEXT("type"), ActorType))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'type' (clase nativa, ruta de Blueprint o ruta de StaticMesh)"));
    }
    FString ActorName;
    Params->TryGetStringField(TEXT("name"), ActorName);

    FString Error;
    UClass* Class = nullptr;
    UStaticMesh* Mesh = nullptr;

    // Una ruta de StaticMesh crea un StaticMeshActor con ese mesh.
    if (ActorType.StartsWith(TEXT("/Game/")) || ActorType.StartsWith(TEXT("/Engine/")))
    {
        FString AssetError;
        if (UObject* Asset = FUnrealMCPCommonUtils::ResolveAsset(ActorType, AssetError))
        {
            if (UStaticMesh* AsMesh = Cast<UStaticMesh>(Asset))
            {
                Mesh = AsMesh;
                Class = AStaticMeshActor::StaticClass();
            }
            else if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
            {
                Class = Blueprint->GeneratedClass;
            }
            else if (UClass* AsClass = Cast<UClass>(Asset))
            {
                Class = AsClass;
            }
        }
    }
    if (!Class)
    {
        Class = FUnrealMCPCommonUtils::ResolveClass(ActorType, Error);
    }
    if (!Class)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FString MeshPath;
    if (!Mesh && Params->TryGetStringField(TEXT("static_mesh"), MeshPath) && !MeshPath.IsEmpty())
    {
        Mesh = Cast<UStaticMesh>(FUnrealMCPCommonUtils::ResolveAsset(MeshPath, Error, UStaticMesh::StaticClass()));
        if (!Mesh)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
        }
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Spawn Actor"));
    AActor* NewActor = SpawnInEditor(Class, ReadTransform(Params), ActorName, Error);
    if (!NewActor)
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    if (Mesh)
    {
        if (AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(NewActor))
        {
            MeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
        }
    }

    return FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleDeleteActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'name'"));
    }

    FString Error;
    AActor* Actor = FUnrealMCPCommonUtils::FindActor(ActorName, Error);
    if (!Actor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    TSharedPtr<FJsonObject> ActorInfo = FUnrealMCPCommonUtils::ActorToJsonObject(Actor);

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Delete Actor"));
    Actor->Modify();
    if (!Actor->GetWorld()->EditorDestroyActor(Actor, true))
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No se pudo borrar %s"), *ActorName));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetObjectField(TEXT("deleted_actor"), ActorInfo);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'name'"));
    }

    FString Error;
    AActor* Actor = FUnrealMCPCommonUtils::FindActor(ActorName, Error);
    if (!Actor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Set Actor Transform"));
    Actor->Modify();
    Actor->SetActorTransform(ReadTransform(Params, Actor->GetTransform()));
    Actor->PostEditMove(true);

    return FUnrealMCPCommonUtils::ActorToJsonObject(Actor, true);
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetActorProperties(const TSharedPtr<FJsonObject>& Params)
{
    FString Error;
    UObject* Target = FUnrealMCPCommonUtils::ResolveTargetObject(Params, Error);
    if (!Target)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FString Filter;
    Params->TryGetStringField(TEXT("filter"), Filter);
    bool bAll = false;
    Params->TryGetBoolField(TEXT("include_all"), bAll);

    TSharedPtr<FJsonObject> Result;
    if (AActor* Actor = Cast<AActor>(Target))
    {
        Result = FUnrealMCPCommonUtils::ActorToJsonObject(Actor, true);
    }
    else
    {
        Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("name"), Target->GetName());
        Result->SetStringField(TEXT("class"), Target->GetClass()->GetName());
        Result->SetStringField(TEXT("path"), Target->GetPathName());
    }
    Result->SetObjectField(TEXT("properties"), FUnrealMCPCommonUtils::ObjectPropertiesToJson(Target, Filter, !bAll));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSetActorProperty(const TSharedPtr<FJsonObject>& Params)
{
    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'property_name'"));
    }
    TSharedPtr<FJsonValue> PropertyValue = Params->TryGetField(TEXT("property_value"));
    if (!PropertyValue.IsValid())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'property_value'"));
    }

    FString Error;
    UObject* Target = FUnrealMCPCommonUtils::ResolveTargetObject(Params, Error);
    if (!Target)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Set Property"));
    if (!FUnrealMCPCommonUtils::SetObjectPropertyByPath(Target, PropertyName, PropertyValue, Error))
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FProperty* Property = nullptr;
    void* ValuePtr = nullptr;
    UObject* Owner = nullptr;
    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("target"), Target->GetPathName());
    ResultObj->SetStringField(TEXT("property"), PropertyName);
    if (FUnrealMCPCommonUtils::ResolvePropertyPath(Target, PropertyName, Property, ValuePtr, Owner, Error))
    {
        ResultObj->SetField(TEXT("new_value"), FUnrealMCPCommonUtils::PropertyValueToJson(Property, ValuePtr));
    }
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName) || BlueprintName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'blueprint_name' (nombre o ruta)"));
    }
    FString ActorName;
    Params->TryGetStringField(TEXT("actor_name"), ActorName);

    FString Error;
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName, Error);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP: Spawn Blueprint Actor"));
    AActor* NewActor = SpawnInEditor(Blueprint->GeneratedClass, ReadTransform(Params), ActorName, Error);
    if (!NewActor)
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }
    return FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleFocusViewport(const TSharedPtr<FJsonObject>& Params)
{
    FLevelEditorViewportClient* ViewportClient = GCurrentLevelEditingViewportClient;
    if (!ViewportClient && GEditor->GetLevelViewportClients().Num() > 0)
    {
        ViewportClient = GEditor->GetLevelViewportClients()[0];
    }
    if (!ViewportClient)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No hay viewport de nivel activo"));
    }

    FString TargetActorName;
    if (Params->TryGetStringField(TEXT("target"), TargetActorName) && !TargetActorName.IsEmpty())
    {
        FString Error;
        AActor* Actor = FUnrealMCPCommonUtils::FindActor(TargetActorName, Error);
        if (!Actor)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
        }
        ViewportClient->FocusViewportOnBox(Actor->GetComponentsBoundingBox(true), true);
    }
    else if (Params->HasField(TEXT("location")))
    {
        double Distance = 1000.0;
        Params->TryGetNumberField(TEXT("distance"), Distance);
        const FVector Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
        const FRotator Rotation = ViewportClient->GetViewRotation();
        ViewportClient->SetViewLocation(Location - Rotation.Vector() * Distance);
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Indica 'target' o 'location'"));
    }

    if (Params->HasField(TEXT("orientation")))
    {
        ViewportClient->SetViewRotation(FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("orientation")));
    }
    ViewportClient->Invalidate();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleTakeScreenshot(const TSharedPtr<FJsonObject>& Params)
{
    // ConvertRelativePathToFull deja rutas como "C:/../../Proyecto" en este proyecto.
    const FString SavedDir = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*FPaths::ProjectSavedDir());
    FString FilePath;
    if (!Params->TryGetStringField(TEXT("filepath"), FilePath) || FilePath.IsEmpty())
    {
        FilePath = SavedDir / TEXT("Screenshots/MCP") / (FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")) + TEXT(".png"));
    }
    if (FPaths::IsRelative(FilePath))
    {
        FilePath = SavedDir / FilePath;
    }
    if (!FilePath.EndsWith(TEXT(".png")))
    {
        FilePath += TEXT(".png");
    }
    FPaths::NormalizeFilename(FilePath);
    FPaths::CollapseRelativeDirectories(FilePath);

    // Con PIE corriendo se captura el viewport del juego; si no, el viewport del nivel activo.
    FViewport* Viewport = nullptr;
    if (GEditor->PlayWorld && GEngine->GameViewport)
    {
        Viewport = GEngine->GameViewport->Viewport;
    }
    if (!Viewport && GCurrentLevelEditingViewportClient)
    {
        Viewport = GCurrentLevelEditingViewportClient->Viewport;
    }
    if (!Viewport)
    {
        Viewport = GEditor->GetActiveViewport();
    }
    if (!Viewport || Viewport->GetSizeXY().X <= 0)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No hay viewport para capturar"));
    }

    // Con el editor en segundo plano el viewport no se redibuja y ReadPixels leería un frame viejo.
    Viewport->Draw(true);
    FlushRenderingCommands();

    FIntPoint Size = Viewport->GetSizeXY();
    TArray<FColor> Bitmap;
    bool bCaptured = false;

    // En PIE se captura el widget de Slate para incluir la UI (AddToViewport); ReadPixels solo ve la escena.
    if (GEditor->PlayWorld && GEngine->GameViewport && FSlateApplication::IsInitialized())
    {
        if (TSharedPtr<SViewport> GameViewportWidget = GEngine->GameViewport->GetGameViewportWidget())
        {
            FIntVector SlateSize;
            if (FSlateApplication::Get().TakeScreenshot(GameViewportWidget.ToSharedRef(), Bitmap, SlateSize) && SlateSize.X > 0)
            {
                Size = FIntPoint(SlateSize.X, SlateSize.Y);
                bCaptured = true;
            }
        }
    }
    if (!bCaptured && !Viewport->ReadPixels(Bitmap, FReadSurfaceDataFlags(), FIntRect(0, 0, Size.X, Size.Y)))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ReadPixels falló"));
    }
    for (FColor& Pixel : Bitmap)
    {
        Pixel.A = 255;
    }

    TArray64<uint8> Png;
    FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Bitmap.GetData(), Bitmap.Num()), Png);
    if (!FFileHelper::SaveArrayToFile(Png, *FilePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No se pudo guardar %s"), *FilePath));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("filepath"), FilePath);
    ResultObj->SetNumberField(TEXT("width"), Size.X);
    ResultObj->SetNumberField(TEXT("height"), Size.Y);
    return ResultObj;
}
