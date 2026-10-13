#include "Commands/UnrealMCPAssetCommands.h"

#include "Commands/UnrealMCPCommonUtils.h"
#include "Commands/UnrealMCPGraphSerializer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"
#include "Factories/Factory.h"
#include "Factories/DataAssetFactory.h"
#include "Engine/DataAsset.h"
#include "Engine/Blueprint.h"
#include "UObject/UObjectIterator.h"

namespace
{
    int32 ReadInt(const TSharedPtr<FJsonObject>& Params, const TCHAR* Name, int32 DefaultValue)
    {
        double Value = 0.0;
        if (Params.IsValid() && Params->TryGetNumberField(Name, Value))
        {
            return FMath::RoundToInt(Value);
        }
        return DefaultValue;
    }

    void AddNames(const TArray<FName>& Names, const TCHAR* Field, TArray<TSharedPtr<FJsonValue>>& Out)
    {
        for (const FName& Name : Names)
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(Field, Name.ToString());
            Out.Add(MakeShared<FJsonValueObject>(Item));
        }
    }
}

TSharedPtr<FJsonObject> FUnrealMCPAssetCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("duplicate_asset")) return HandleDuplicateAsset(Params);
    if (CommandType == TEXT("save_assets")) return HandleSaveAssets(Params);
    if (CommandType == TEXT("find_asset_references")) return HandleFindAssetReferences(Params);
    if (CommandType == TEXT("get_editor_log")) return HandleGetEditorLog(Params);
    if (CommandType == TEXT("search_assets")) return HandleSearchAssets(Params);
    if (CommandType == TEXT("get_asset_info")) return HandleGetAssetInfo(Params);
    if (CommandType == TEXT("create_asset")) return HandleCreateAsset(Params);
    if (CommandType == TEXT("rename_asset")) return HandleRenameAsset(Params);
    if (CommandType == TEXT("delete_asset")) return HandleDeleteAsset(Params);
    if (CommandType == TEXT("create_folder")) return HandleCreateFolder(Params);
    if (CommandType == TEXT("import_asset")) return HandleImportAsset(Params);
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Comando de asset desconocido: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPAssetCommands::HandleDuplicateAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString SourcePath;
    FString DestPath;
    if (!Params.IsValid()
        || !Params->TryGetStringField(TEXT("source_path"), SourcePath) || SourcePath.IsEmpty()
        || !Params->TryGetStringField(TEXT("dest_path"), DestPath) || DestPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Faltan 'source_path' y 'dest_path'"));
    }

    const FString Source = FPackageName::ObjectPathToPackageName(UnrealMCPGraph::NormalizeObjectPath(SourcePath));
    FString Dest = DestPath;
    Dest.TrimStartAndEndInline();
    if (Dest.Contains(TEXT(".")))
    {
        Dest = FPackageName::ObjectPathToPackageName(Dest);
    }
    if (UEditorAssetLibrary::DoesAssetExist(Dest))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("El destino ya existe: %s"), *Dest));
    }
    if (!UEditorAssetLibrary::DoesAssetExist(Source))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No se encontró el origen: %s"), *Source));
    }

    FUnrealMCPEditTransaction Transaction(TEXT("MCP duplicate asset"));
    UObject* Copy = UEditorAssetLibrary::DuplicateAsset(Source, Dest);
    if (!Copy)
    {
        Transaction.Cancel();
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se pudo duplicar el asset"));
    }
    Copy->GetOutermost()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("source"), Source);
    Data->SetStringField(TEXT("path"), Copy->GetPathName());
    Data->SetStringField(TEXT("class"), Copy->GetClass()->GetName());
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPAssetCommands::HandleSaveAssets(const TSharedPtr<FJsonObject>& Params)
{
    const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
    if (!Params.IsValid() || !Params->TryGetArrayField(TEXT("paths"), Paths) || !Paths)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'paths'"));
    }

    TArray<TSharedPtr<FJsonValue>> Results;
    for (const TSharedPtr<FJsonValue>& Value : *Paths)
    {
        const FString Path = FPackageName::ObjectPathToPackageName(UnrealMCPGraph::NormalizeObjectPath(Value->AsString()));
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("path"), Path);
        UObject* Asset = UEditorAssetLibrary::LoadAsset(Path);
        const bool bSaved = Asset && UEditorAssetLibrary::SaveLoadedAsset(Asset, false);
        Item->SetBoolField(TEXT("saved"), bSaved);
        Results.Add(MakeShared<FJsonValueObject>(Item));
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("results"), Results);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPAssetCommands::HandleFindAssetReferences(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params.IsValid() || !Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'asset_path'"));
    }
    bool bReverse = false;
    if (Params->HasField(TEXT("reverse")))
    {
        bReverse = Params->GetBoolField(TEXT("reverse"));
    }
    const FString PackageName = FPackageName::ObjectPathToPackageName(UnrealMCPGraph::NormalizeObjectPath(AssetPath));
    FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FName> Names;
    if (bReverse)
    {
        AssetRegistry.Get().GetReferencers(FName(*PackageName), Names);
    }
    else
    {
        AssetRegistry.Get().GetDependencies(FName(*PackageName), Names);
    }

    TArray<TSharedPtr<FJsonValue>> Items;
    AddNames(Names, TEXT("package"), Items);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("package"), PackageName);
    Data->SetBoolField(TEXT("reverse"), bReverse);
    Data->SetNumberField(TEXT("count"), Items.Num());
    Data->SetArrayField(TEXT("packages"), Items);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPAssetCommands::HandleGetEditorLog(const TSharedPtr<FJsonObject>& Params)
{
    FString Filter;
    if (Params.IsValid())
    {
        Params->TryGetStringField(TEXT("filter"), Filter);
    }
    const int32 LineCount = FMath::Clamp(ReadInt(Params, TEXT("lines"), 80), 1, 1000);
    bool bErrorsOnly = true;
    if (Params.IsValid())
    {
        Params->TryGetBoolField(TEXT("errors_only"), bErrorsOnly);
    }
    const FString EditorLogFile = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(
        *(FPaths::ProjectLogDir() / FString(FApp::GetProjectName()) + TEXT(".log")));

    // El editor tiene el log abierto para escritura: hay que pedir lectura compartida.
    GLog->Flush();
    TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileReader(*EditorLogFile, FILEREAD_AllowWrite));
    if (!Archive)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No se pudo leer %s"), *EditorLogFile));
    }
    const int64 Size = Archive->TotalSize();
    const int64 Start = FMath::Max<int64>(0, Size - 256 * 1024);
    Archive->Seek(Start);
    TArray<uint8> Bytes;
    Bytes.SetNumUninitialized(static_cast<int32>(Size - Start) + 1);
    Archive->Serialize(Bytes.GetData(), Size - Start);
    Bytes.Last() = 0;
    FString Text = UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData()));

    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines);
    TArray<TSharedPtr<FJsonValue>> Kept;
    for (int32 Index = Lines.Num() - 1; Index >= 0 && Kept.Num() < LineCount; --Index)
    {
        const FString& Line = Lines[Index];
        if (!Filter.IsEmpty() && !Line.Contains(Filter, ESearchCase::IgnoreCase))
        {
            continue;
        }
        if (Filter.IsEmpty() && bErrorsOnly && !Line.Contains(TEXT("Error")) && !Line.Contains(TEXT("Warning")))
        {
            continue;
        }
        Kept.Insert(MakeShared<FJsonValueString>(Line.Left(500)), 0);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("path"), EditorLogFile);
    Data->SetNumberField(TEXT("count"), Kept.Num());
    Data->SetArrayField(TEXT("lines"), Kept);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Data);
}

TSharedPtr<FJsonObject> FUnrealMCPAssetCommands::HandleSearchAssets(const TSharedPtr<FJsonObject>& Params)
{
    FString Query, ClassName, Path = TEXT("/Game");
    Params->TryGetStringField(TEXT("query"), Query);
    Params->TryGetStringField(TEXT("class"), ClassName);
    Params->TryGetStringField(TEXT("path"), Path);
    const int32 Limit = FMath::Clamp(ReadInt(Params, TEXT("limit"), 100), 1, 2000);
    bool bRecursive = true;
    Params->TryGetBoolField(TEXT("recursive"), bRecursive);

    TArray<FString> Words;
    Query.ParseIntoArrayWS(Words);

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    FARFilter Filter;
    Filter.bRecursivePaths = bRecursive;
    Filter.PackagePaths.Add(FName(*Path));
    TArray<FAssetData> Assets;
    Registry.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> Items;
    int32 Total = 0;
    for (const FAssetData& Asset : Assets)
    {
        if (Asset.IsRedirector())
        {
            continue;
        }
        const FString AssetClass = Asset.AssetClassPath.GetAssetName().ToString();
        if (!ClassName.IsEmpty() && !AssetClass.Equals(ClassName, ESearchCase::IgnoreCase))
        {
            continue;
        }
        const FString AssetName = Asset.AssetName.ToString();
        bool bMatches = true;
        for (const FString& Word : Words)
        {
            if (!AssetName.Contains(Word) && !Asset.PackagePath.ToString().Contains(Word))
            {
                bMatches = false;
                break;
            }
        }
        if (!bMatches)
        {
            continue;
        }

        ++Total;
        if (Items.Num() < Limit)
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("name"), AssetName);
            Item->SetStringField(TEXT("path"), Asset.GetSoftObjectPath().ToString());
            Item->SetStringField(TEXT("class"), AssetClass);
            Items.Add(MakeShared<FJsonValueObject>(Item));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("total"), Total);
    Result->SetArrayField(TEXT("assets"), Items);
    if (Total > Items.Num())
    {
        Result->SetStringField(TEXT("note"), TEXT("Resultados truncados: acota con query, class o path, o sube limit"));
    }
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAssetCommands::HandleGetAssetInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'asset_path'"));
    }

    FString Error;
    UObject* Asset = FUnrealMCPCommonUtils::ResolveAsset(AssetPath, Error);
    if (!Asset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), Asset->GetPathName());
    Result->SetStringField(TEXT("class"), Asset->GetClass()->GetPathName());
    Result->SetBoolField(TEXT("dirty"), Asset->GetOutermost()->IsDirty());

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData Data = Registry.GetAssetByObjectPath(FSoftObjectPath(Asset));
    if (Data.IsValid())
    {
        TSharedPtr<FJsonObject> Tags = MakeShared<FJsonObject>();
        int32 TagCount = 0;
        for (const auto& Pair : Data.TagsAndValues)
        {
            if (TagCount++ >= 60)
            {
                break;
            }
            Tags->SetStringField(Pair.Key.ToString(), Pair.Value.AsString().Left(300));
        }
        Result->SetObjectField(TEXT("tags"), Tags);

        TArray<FName> Referencers;
        Registry.GetReferencers(Data.PackageName, Referencers);
        Result->SetNumberField(TEXT("referencer_count"), Referencers.Num());
    }

    bool bProperties = false;
    Params->TryGetBoolField(TEXT("include_properties"), bProperties);
    FString Filter;
    Params->TryGetStringField(TEXT("filter"), Filter);
    if (bProperties || !Filter.IsEmpty())
    {
        UObject* Target = Asset;
        // En un Blueprint lo útil son los valores por defecto de la clase generada.
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
        {
            if (Blueprint->GeneratedClass)
            {
                Target = Blueprint->GeneratedClass->GetDefaultObject();
            }
        }
        Result->SetObjectField(TEXT("properties"), FUnrealMCPCommonUtils::ObjectPropertiesToJson(Target, Filter, true));
    }
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAssetCommands::HandleCreateAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString ClassName, Name, Folder;
    if (!Params->TryGetStringField(TEXT("class"), ClassName) || !Params->TryGetStringField(TEXT("name"), Name))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Faltan 'class' y 'name'"));
    }
    if (!Params->TryGetStringField(TEXT("folder"), Folder))
    {
        Folder = TEXT("/Game");
    }

    FString Error;
    UClass* AssetClass = FUnrealMCPCommonUtils::ResolveClass(ClassName, Error);
    if (!AssetClass)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Error);
    }

    const FString PackagePath = FUnrealMCPCommonUtils::MakePackagePath(Folder, Name);
    if (FPackageName::DoesPackageExist(PackagePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Ya existe %s"), *PackagePath));
    }

    UFactory* Factory = nullptr;
    if (AssetClass->IsChildOf(UDataAsset::StaticClass()))
    {
        UDataAssetFactory* DataFactory = NewObject<UDataAssetFactory>();
        DataFactory->DataAssetClass = AssetClass;
        Factory = DataFactory;
    }
    else
    {
        for (TObjectIterator<UClass> It; It; ++It)
        {
            if (!It->IsChildOf(UFactory::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
            {
                continue;
            }
            UFactory* Default = It->GetDefaultObject<UFactory>();
            if (Default && Default->CanCreateNew() && Default->GetSupportedClass() == AssetClass)
            {
                Factory = NewObject<UFactory>(GetTransientPackage(), *It);
                break;
            }
        }
    }
    if (!Factory)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
            TEXT("No hay factory para crear %s. Para Blueprints usa create_blueprint, para widgets create_umg_widget_blueprint, o prueba execute_python"),
            *AssetClass->GetName()));
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    UObject* Asset = AssetTools.CreateAsset(Name, FPackageName::GetLongPackagePath(PackagePath), AssetClass, Factory);
    if (!Asset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("CreateAsset falló"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), Asset->GetPathName());
    Result->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
    Result->SetStringField(TEXT("factory"), Factory->GetClass()->GetName());
    Result->SetStringField(TEXT("note"), TEXT("Sin guardar: usa save_assets"));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAssetCommands::HandleRenameAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString SourcePath, NewPath, NewName;
    if (!Params->TryGetStringField(TEXT("asset_path"), SourcePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'asset_path'"));
    }
    Params->TryGetStringField(TEXT("new_path"), NewPath);
    Params->TryGetStringField(TEXT("new_name"), NewName);

    const FString Source = FPackageName::ObjectPathToPackageName(UnrealMCPGraph::NormalizeObjectPath(SourcePath));
    if (!UEditorAssetLibrary::DoesAssetExist(Source))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No existe %s"), *Source));
    }

    FString Dest;
    if (!NewPath.IsEmpty())
    {
        Dest = NewPath.Contains(TEXT(".")) ? FPackageName::ObjectPathToPackageName(NewPath) : NewPath;
    }
    else if (!NewName.IsEmpty())
    {
        Dest = FPackageName::GetLongPackagePath(Source) / NewName;
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Indica 'new_path' o 'new_name'"));
    }
    if (UEditorAssetLibrary::DoesAssetExist(Dest))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Ya existe %s"), *Dest));
    }

    if (!UEditorAssetLibrary::RenameAsset(Source, Dest))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("RenameAsset falló (revisa el log)"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("old_path"), Source);
    Result->SetStringField(TEXT("new_path"), Dest);
    Result->SetStringField(TEXT("note"), TEXT("Se dejó un redirector; las referencias se actualizan al guardar"));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAssetCommands::HandleDeleteAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'asset_path'"));
    }
    bool bForce = false;
    Params->TryGetBoolField(TEXT("force"), bForce);

    const FString PackageName = FPackageName::ObjectPathToPackageName(UnrealMCPGraph::NormalizeObjectPath(AssetPath));
    if (!UEditorAssetLibrary::DoesAssetExist(PackageName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No existe %s"), *PackageName));
    }

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FName> Referencers;
    Registry.GetReferencers(FName(*PackageName), Referencers);
    Referencers.Remove(FName(*PackageName));
    if (Referencers.Num() > 0 && !bForce)
    {
        TArray<FString> Names;
        for (const FName& Name : Referencers)
        {
            Names.Add(Name.ToString());
        }
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
            TEXT("%s está referenciado por %d assets: %s. Pasa force=true para borrarlo igual"),
            *PackageName, Names.Num(), *FString::Join(Names, TEXT(", ")).Left(1500)));
    }

    if (!UEditorAssetLibrary::DeleteAsset(PackageName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("DeleteAsset falló (¿está abierto o bloqueado?)"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("deleted"), PackageName);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAssetCommands::HandleCreateFolder(const TSharedPtr<FJsonObject>& Params)
{
    FString Path;
    if (!Params->TryGetStringField(TEXT("path"), Path) || !Path.StartsWith(TEXT("/Game")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Falta 'path' (debe empezar con /Game)"));
    }
    if (!UEditorAssetLibrary::MakeDirectory(Path))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No se pudo crear %s"), *Path));
    }
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path"), Path);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAssetCommands::HandleImportAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString File, Destination;
    if (!Params->TryGetStringField(TEXT("file"), File) || !Params->TryGetStringField(TEXT("destination"), Destination))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Faltan 'file' (ruta en disco) y 'destination' (carpeta /Game/...)"));
    }
    if (!FPaths::FileExists(File))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("No existe el archivo %s"), *File));
    }

    UAssetImportTask* Task = NewObject<UAssetImportTask>();
    Task->Filename = File;
    Task->DestinationPath = Destination;
    Params->TryGetStringField(TEXT("name"), Task->DestinationName);
    Task->bAutomated = true;
    Task->bSave = false;
    Params->TryGetBoolField(TEXT("replace"), Task->bReplaceExisting);

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    AssetTools.ImportAssetTasks({ Task });

    if (Task->ImportedObjectPaths.Num() == 0)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No se importó nada (formato no soportado o error; revisa get_editor_log)"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("imported"), FUnrealMCPCommonUtils::StringArrayToJson(Task->ImportedObjectPaths));
    Result->SetStringField(TEXT("note"), TEXT("Sin guardar: usa save_assets"));
    return Result;
}
