// Búsqueda de actores/assets y reflexión genérica de propiedades para FUnrealMCPCommonUtils.

#include "Commands/UnrealMCPCommonUtils.h"
#include "Commands/UnrealMCPGraphSerializer.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ---------------------------------------------------------------------------
// World / actor lookup
// ---------------------------------------------------------------------------

UWorld* FUnrealMCPCommonUtils::GetEditorWorld(bool bPreferPIE)
{
	if (!GEditor)
	{
		return nullptr;
	}
	if (bPreferPIE && GEditor->PlayWorld)
	{
		return GEditor->PlayWorld;
	}
	return GEditor->GetEditorWorldContext().World();
}

FString FUnrealMCPCommonUtils::GetActorDisplayName(const AActor* Actor)
{
	return Actor ? Actor->GetActorLabel() : FString();
}

AActor* FUnrealMCPCommonUtils::FindActor(const FString& NameOrLabel, FString& OutError, UWorld* World)
{
	if (!World)
	{
		World = GetEditorWorld();
	}
	if (!World)
	{
		OutError = TEXT("No hay mundo del editor");
		return nullptr;
	}

	const FString Wanted = NameOrLabel.TrimStartAndEnd();
	if (Wanted.IsEmpty())
	{
		OutError = TEXT("Falta el nombre del actor");
		return nullptr;
	}

	TArray<AActor*> LabelMatches;
	AActor* NameMatch = nullptr;
	AActor* PathMatch = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}
		if (GetActorDisplayName(Actor).Equals(Wanted, ESearchCase::IgnoreCase))
		{
			LabelMatches.Add(Actor);
		}
		if (!NameMatch && Actor->GetName().Equals(Wanted, ESearchCase::IgnoreCase))
		{
			NameMatch = Actor;
		}
		if (!PathMatch && Actor->GetPathName().Equals(Wanted, ESearchCase::IgnoreCase))
		{
			PathMatch = Actor;
		}
	}

	if (PathMatch)
	{
		return PathMatch;
	}
	if (NameMatch)
	{
		return NameMatch;
	}
	if (LabelMatches.Num() == 1)
	{
		return LabelMatches[0];
	}
	if (LabelMatches.Num() > 1)
	{
		TArray<FString> Names;
		for (AActor* Actor : LabelMatches)
		{
			Names.Add(Actor->GetName());
		}
		OutError = FString::Printf(TEXT("Hay %d actores con la etiqueta '%s'. Usa el nombre interno: %s"),
			LabelMatches.Num(), *Wanted, *FString::Join(Names, TEXT(", ")));
		return nullptr;
	}

	OutError = FString::Printf(TEXT("No se encontró el actor '%s' en %s"), *Wanted, *World->GetMapName());
	return nullptr;
}

UActorComponent* FUnrealMCPCommonUtils::FindComponent(AActor* Actor, const FString& ComponentName)
{
	if (!Actor || ComponentName.IsEmpty())
	{
		return nullptr;
	}

	TInlineComponentArray<UActorComponent*> Components(Actor);
	for (UActorComponent* Component : Components)
	{
		if (Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			return Component;
		}
	}
	for (UActorComponent* Component : Components)
	{
		if (Component && Component->GetClass()->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			return Component;
		}
	}
	return nullptr;
}

UObject* FUnrealMCPCommonUtils::ResolveTargetObject(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	FString AssetPath;
	if (Params->TryGetStringField(TEXT("asset_path"), AssetPath) && !AssetPath.IsEmpty())
	{
		return ResolveAsset(AssetPath, OutError);
	}

	FString ActorName;
	if (!Params->TryGetStringField(TEXT("actor"), ActorName))
	{
		Params->TryGetStringField(TEXT("name"), ActorName);
	}
	if (ActorName.IsEmpty())
	{
		OutError = TEXT("Indica 'actor' (y opcionalmente 'component') o 'asset_path'");
		return nullptr;
	}

	bool bPIE = false;
	Params->TryGetBoolField(TEXT("pie"), bPIE);
	AActor* Actor = FindActor(ActorName, OutError, GetEditorWorld(bPIE));
	if (!Actor)
	{
		return nullptr;
	}

	FString ComponentName;
	if (Params->TryGetStringField(TEXT("component"), ComponentName) && !ComponentName.IsEmpty())
	{
		UActorComponent* Component = FindComponent(Actor, ComponentName);
		if (!Component)
		{
			OutError = FString::Printf(TEXT("El actor '%s' no tiene el componente '%s'"), *ActorName, *ComponentName);
			return nullptr;
		}
		return Component;
	}
	return Actor;
}

// ---------------------------------------------------------------------------
// Generic reflection
// ---------------------------------------------------------------------------

namespace
{
	FProperty* FindPropertyLoose(const UStruct* Struct, const FString& Name)
	{
		if (!Struct)
		{
			return nullptr;
		}
		if (FProperty* Exact = FindFProperty<FProperty>(Struct, *Name))
		{
			return Exact;
		}
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Property = *It;
			// GetAuthoredName quita el sufijo que los User Defined Structs agregan a sus campos.
			if (Property->GetName().Equals(Name, ESearchCase::IgnoreCase) ||
				Property->GetAuthoredName().Equals(Name, ESearchCase::IgnoreCase))
			{
				return Property;
			}
		}
		return nullptr;
	}

	bool ParseSegment(const FString& Segment, FString& OutName, int32& OutIndex)
	{
		OutIndex = INDEX_NONE;
		int32 Open = INDEX_NONE;
		if (!Segment.FindChar(TEXT('['), Open))
		{
			OutName = Segment;
			return true;
		}
		if (!Segment.EndsWith(TEXT("]")))
		{
			return false;
		}
		OutName = Segment.Left(Open);
		const FString IndexText = Segment.Mid(Open + 1, Segment.Len() - Open - 2);
		if (!IndexText.IsNumeric())
		{
			return false;
		}
		OutIndex = FCString::Atoi(*IndexText);
		return true;
	}

	/** Permite [x, y, z] para vectores, rotadores y colores en vez de {"X":..}. */
	bool JsonArrayToStructShortcut(FStructProperty* StructProperty, void* ValuePtr, const TSharedPtr<FJsonValue>& Value)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!StructProperty || !Value.IsValid() || !Value->TryGetArray(Array))
		{
			return false;
		}

		auto Num = [Array](int32 Index, double Default) -> double
		{
			return Array->IsValidIndex(Index) ? (*Array)[Index]->AsNumber() : Default;
		};

		const UScriptStruct* Struct = StructProperty->Struct;
		if (Struct == TBaseStructure<FVector>::Get() && Array->Num() >= 3)
		{
			*static_cast<FVector*>(ValuePtr) = FVector(Num(0, 0), Num(1, 0), Num(2, 0));
			return true;
		}
		if (Struct == TBaseStructure<FRotator>::Get() && Array->Num() >= 3)
		{
			*static_cast<FRotator*>(ValuePtr) = FRotator(Num(0, 0), Num(1, 0), Num(2, 0));
			return true;
		}
		if (Struct == TBaseStructure<FVector2D>::Get() && Array->Num() >= 2)
		{
			*static_cast<FVector2D*>(ValuePtr) = FVector2D(Num(0, 0), Num(1, 0));
			return true;
		}
		if (Struct == TBaseStructure<FLinearColor>::Get() && Array->Num() >= 3)
		{
			*static_cast<FLinearColor*>(ValuePtr) = FLinearColor(Num(0, 0), Num(1, 0), Num(2, 0), Num(3, 1));
			return true;
		}
		if (Struct == TBaseStructure<FColor>::Get() && Array->Num() >= 3)
		{
			*static_cast<FColor*>(ValuePtr) = FColor(Num(0, 0), Num(1, 0), Num(2, 0), Num(3, 255));
			return true;
		}
		return false;
	}
}

bool FUnrealMCPCommonUtils::ResolvePropertyPath(UObject* Object, const FString& Path, FProperty*& OutProperty, void*& OutValuePtr,
                                                UObject*& OutOwner, FString& OutError)
{
	OutProperty = nullptr;
	OutValuePtr = nullptr;
	OutOwner = Object;
	if (!Object)
	{
		OutError = TEXT("Objeto inválido");
		return false;
	}

	TArray<FString> Segments;
	Path.ParseIntoArray(Segments, TEXT("."), true);
	if (Segments.Num() == 0)
	{
		OutError = TEXT("Ruta de propiedad vacía");
		return false;
	}

	const UStruct* CurrentStruct = Object->GetClass();
	void* Container = Object;

	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		FString Name;
		int32 Index = INDEX_NONE;
		if (!ParseSegment(Segments[SegmentIndex], Name, Index))
		{
			OutError = FString::Printf(TEXT("Segmento inválido '%s'"), *Segments[SegmentIndex]);
			return false;
		}

		FProperty* Property = FindPropertyLoose(CurrentStruct, Name);
		if (!Property && SegmentIndex == 0)
		{
			// Atajo: en un actor, "MiComponente.Propiedad" entra en el componente por nombre.
			if (AActor* Actor = Cast<AActor>(Object))
			{
				if (UActorComponent* Component = FindComponent(Actor, Name))
				{
					if (Segments.Num() == 1)
					{
						OutError = FString::Printf(TEXT("'%s' es un componente; indica una propiedad (p. ej. %s.RelativeLocation)"), *Name, *Name);
						return false;
					}
					OutOwner = Component;
					CurrentStruct = Component->GetClass();
					Container = Component;
					continue;
				}
			}
		}
		if (!Property)
		{
			OutError = FString::Printf(TEXT("No existe la propiedad '%s' en %s"), *Name, *CurrentStruct->GetName());
			return false;
		}

		void* ValuePtr = nullptr;
		if (Index != INDEX_NONE)
		{
			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Container));
				if (!Helper.IsValidIndex(Index))
				{
					OutError = FString::Printf(TEXT("Índice %d fuera de rango en '%s' (tamaño %d)"), Index, *Name, Helper.Num());
					return false;
				}
				ValuePtr = Helper.GetRawPtr(Index);
				Property = ArrayProperty->Inner;
			}
			else if (Property->ArrayDim > 1)
			{
				if (Index < 0 || Index >= Property->ArrayDim)
				{
					OutError = FString::Printf(TEXT("Índice %d fuera de rango en '%s'"), Index, *Name);
					return false;
				}
				ValuePtr = Property->ContainerPtrToValuePtr<void>(Container, Index);
			}
			else
			{
				OutError = FString::Printf(TEXT("'%s' no es un array"), *Name);
				return false;
			}
		}
		else
		{
			ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
		}

		if (SegmentIndex == Segments.Num() - 1)
		{
			OutProperty = Property;
			OutValuePtr = ValuePtr;
			return true;
		}

		if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			CurrentStruct = StructProperty->Struct;
			Container = ValuePtr;
		}
		else if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			UObject* SubObject = ObjectProperty->GetObjectPropertyValue(ValuePtr);
			if (!SubObject)
			{
				OutError = FString::Printf(TEXT("'%s' es null"), *Name);
				return false;
			}
			OutOwner = SubObject;
			CurrentStruct = SubObject->GetClass();
			Container = SubObject;
		}
		else
		{
			OutError = FString::Printf(TEXT("No se puede entrar en '%s' (%s)"), *Name, *Property->GetCPPType());
			return false;
		}
	}

	OutError = TEXT("Ruta de propiedad inválida");
	return false;
}

TSharedPtr<FJsonValue> FUnrealMCPCommonUtils::PropertyValueToJson(FProperty* Property, const void* ValuePtr)
{
	if (!Property || !ValuePtr)
	{
		return MakeShared<FJsonValueNull>();
	}

	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		if (!Property->IsA<FSoftObjectProperty>())
		{
			UObject* Value = ObjectProperty->GetObjectPropertyValue(ValuePtr);
			if (!Value)
			{
				return MakeShared<FJsonValueNull>();
			}
			return MakeShared<FJsonValueString>(Value->GetPathName());
		}
	}

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
		constexpr int32 MaxElements = 100;
		TArray<TSharedPtr<FJsonValue>> Elements;
		for (int32 Index = 0; Index < FMath::Min(Helper.Num(), MaxElements); ++Index)
		{
			Elements.Add(PropertyValueToJson(ArrayProperty->Inner, Helper.GetRawPtr(Index)));
		}
		if (Helper.Num() > MaxElements)
		{
			Elements.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("... %d elementos más"), Helper.Num() - MaxElements)));
		}
		return MakeShared<FJsonValueArray>(Elements);
	}

	if (TSharedPtr<FJsonValue> Converted = FJsonObjectConverter::UPropertyToJsonValue(Property, ValuePtr))
	{
		return Converted;
	}

	FString Text;
	Property->ExportTextItem_Direct(Text, ValuePtr, nullptr, nullptr, PPF_None);
	return MakeShared<FJsonValueString>(Text);
}

bool FUnrealMCPCommonUtils::SetPropertyValueFromJson(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	if (!Property || !ValuePtr || !Value.IsValid())
	{
		OutError = TEXT("Propiedad o valor inválido");
		return false;
	}

	if (JsonArrayToStructShortcut(CastField<FStructProperty>(Property), ValuePtr, Value))
	{
		return true;
	}

	// Referencias a objetos: acepta nombre corto o ruta sin ".Asset".
	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		if (Value->Type == EJson::Null)
		{
			ObjectProperty->SetObjectPropertyValue(ValuePtr, nullptr);
			return true;
		}
		FString Text;
		if (Value->TryGetString(Text))
		{
			if (Text.IsEmpty() || Text.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{
				ObjectProperty->SetObjectPropertyValue(ValuePtr, nullptr);
				return true;
			}
			FString ResolveError;
			UObject* Asset = ResolveAsset(Text, ResolveError, ObjectProperty->PropertyClass);
			if (!Asset)
			{
				OutError = ResolveError;
				return false;
			}
			ObjectProperty->SetObjectPropertyValue(ValuePtr, Asset);
			return true;
		}
	}

	if (FJsonObjectConverter::JsonValueToUProperty(Value, Property, ValuePtr, 0, 0))
	{
		return true;
	}

	// Último recurso: el formato de copiar/pegar de Unreal, p. ej. "(X=1,Y=2,Z=3)".
	FString Text;
	if (!Value->TryGetString(Text))
	{
		if (Value->Type == EJson::Number)
		{
			Text = FString::SanitizeFloat(Value->AsNumber());
		}
		else if (Value->Type == EJson::Boolean)
		{
			Text = Value->AsBool() ? TEXT("True") : TEXT("False");
		}
	}
	if (!Text.IsEmpty() && Property->ImportText_Direct(*Text, ValuePtr, nullptr, PPF_None) != nullptr)
	{
		return true;
	}

	OutError = FString::Printf(TEXT("No se pudo convertir el valor al tipo %s"), *Property->GetCPPType());
	return false;
}

TSharedPtr<FJsonObject> FUnrealMCPCommonUtils::ObjectPropertiesToJson(UObject* Object, const FString& Filter, bool bEditableOnly, int32 MaxProperties)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!Object)
	{
		return Result;
	}

	int32 Count = 0;
	bool bTruncated = false;
	for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
	{
		FProperty* Property = *It;
		if (Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}
		if (bEditableOnly && !Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
		{
			continue;
		}
		const FString Name = Property->GetAuthoredName();
		if (!Filter.IsEmpty() && !Name.Contains(Filter))
		{
			continue;
		}
		if (Count >= MaxProperties)
		{
			bTruncated = true;
			break;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("type"), Property->GetCPPType());
		const FString Category = Property->GetMetaData(TEXT("Category"));
		if (!Category.IsEmpty())
		{
			Entry->SetStringField(TEXT("category"), Category);
		}
		if (Property->ArrayDim == 1)
		{
			Entry->SetField(TEXT("value"), PropertyValueToJson(Property, Property->ContainerPtrToValuePtr<void>(Object)));
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
			{
				Values.Add(PropertyValueToJson(Property, Property->ContainerPtrToValuePtr<void>(Object, Index)));
			}
			Entry->SetArrayField(TEXT("value"), Values);
		}
		Result->SetObjectField(Name, Entry);
		++Count;
	}

	if (bTruncated)
	{
		Result->SetStringField(TEXT("_truncated"), TEXT("Hay más propiedades; usa 'filter' para acotar"));
	}
	return Result;
}

bool FUnrealMCPCommonUtils::SetObjectPropertyByPath(UObject* Object, const FString& Path, const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	FProperty* Property = nullptr;
	void* ValuePtr = nullptr;
	UObject* Owner = nullptr;
	if (!ResolvePropertyPath(Object, Path, Property, ValuePtr, Owner, OutError))
	{
		return false;
	}

	// Se notifica al editor la propiedad de primer nivel dentro del dueño (p. ej. RelativeLocation, no X).
	FProperty* TopProperty = nullptr;
	{
		TArray<FString> Segments;
		Path.ParseIntoArray(Segments, TEXT("."), true);
		for (const FString& Segment : Segments)
		{
			FString Name;
			int32 Index = INDEX_NONE;
			ParseSegment(Segment, Name, Index);
			if (FProperty* Candidate = FindPropertyLoose(Owner->GetClass(), Name))
			{
				TopProperty = Candidate;
				break;
			}
		}
	}

	Owner->Modify();
	Owner->PreEditChange(TopProperty);

	const bool bOk = SetPropertyValueFromJson(Property, ValuePtr, Value, OutError);

	FPropertyChangedEvent ChangedEvent(TopProperty ? TopProperty : Property, EPropertyChangeType::ValueSet);
	Owner->PostEditChangeProperty(ChangedEvent);
	if (Owner != Object)
	{
		Object->Modify();
		Object->PostEditChange();
	}
	Owner->MarkPackageDirty();
	return bOk;
}

// ---------------------------------------------------------------------------
// Asset utilities
// ---------------------------------------------------------------------------

UObject* FUnrealMCPCommonUtils::ResolveAsset(const FString& NameOrPath, FString& OutError, UClass* RequiredClass)
{
	FString Name = NameOrPath.TrimStartAndEnd();
	if (Name.IsEmpty())
	{
		OutError = TEXT("Falta la ruta del asset");
		return nullptr;
	}

	// Formato de referencia copiada del editor: Class'/Game/Path/Asset.Asset'
	int32 Quote = INDEX_NONE;
	if (Name.FindChar(TEXT('\''), Quote) && Name.EndsWith(TEXT("'")))
	{
		Name = Name.Mid(Quote + 1, Name.Len() - Quote - 2);
	}

	auto CheckClass = [&OutError, RequiredClass](UObject* Found) -> UObject*
	{
		if (Found && RequiredClass && !Found->IsA(RequiredClass))
		{
			OutError = FString::Printf(TEXT("%s es un %s, se esperaba %s"),
				*Found->GetPathName(), *Found->GetClass()->GetName(), *RequiredClass->GetName());
			return nullptr;
		}
		return Found;
	};

	if (Name.StartsWith(TEXT("/")))
	{
		const FString ObjectPath = UnrealMCPGraph::NormalizeObjectPath(Name);
		UObject* Found = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
		if (!Found)
		{
			Found = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath, nullptr, LOAD_NoWarn);
		}
		if (!Found)
		{
			OutError = FString::Printf(TEXT("No se encontró el asset %s"), *ObjectPath);
			return nullptr;
		}
		return CheckClass(Found);
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(TEXT("/Game"));
	if (RequiredClass)
	{
		Filter.ClassPaths.Add(RequiredClass->GetClassPathName());
		Filter.bRecursiveClasses = true;
	}
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);

	TArray<FAssetData> Matches;
	for (const FAssetData& Asset : Assets)
	{
		if (!Asset.IsRedirector() && Asset.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase))
		{
			Matches.Add(Asset);
		}
	}

	if (Matches.Num() == 1)
	{
		return CheckClass(Matches[0].GetAsset());
	}
	if (Matches.Num() > 1)
	{
		TArray<FString> Paths;
		for (const FAssetData& Asset : Matches)
		{
			Paths.Add(Asset.GetSoftObjectPath().ToString());
		}
		OutError = FString::Printf(TEXT("Hay varios assets llamados %s: %s"), *Name, *FString::Join(Paths, TEXT(", ")));
		return nullptr;
	}

	OutError = FString::Printf(TEXT("No se encontró el asset '%s'"), *Name);
	return nullptr;
}

FString FUnrealMCPCommonUtils::MakePackagePath(const FString& Folder, const FString& AssetName)
{
	FString Clean = Folder.TrimStartAndEnd();
	if (Clean.IsEmpty())
	{
		Clean = TEXT("/Game");
	}
	if (!Clean.StartsWith(TEXT("/")))
	{
		Clean = TEXT("/Game/") + Clean;
	}
	while (Clean.EndsWith(TEXT("/")))
	{
		Clean.LeftChopInline(1);
	}
	return Clean + TEXT("/") + AssetName;
}

TArray<TSharedPtr<FJsonValue>> FUnrealMCPCommonUtils::StringArrayToJson(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FString& Value : Values)
	{
		Out.Add(MakeShared<FJsonValueString>(Value));
	}
	return Out;
}
