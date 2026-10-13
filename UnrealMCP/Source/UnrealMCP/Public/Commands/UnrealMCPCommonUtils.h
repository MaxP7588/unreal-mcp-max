#pragma once

#include "CoreMinimal.h"
#include "Json.h"
#include "ScopedTransaction.h"

// Forward declarations
class AActor;
class UActorComponent;
class UWorld;
class FProperty;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UK2Node_Event;
class UK2Node_CallFunction;
class UK2Node_VariableGet;
class UK2Node_VariableSet;
class UK2Node_InputAction;
class UK2Node_Self;
class UFunction;
class UClass;
struct FEdGraphPinType;

/**
 * Undo scope for MCP edits. Does not save the asset.
 */
class UNREALMCP_API FUnrealMCPEditTransaction
{
public:
    explicit FUnrealMCPEditTransaction(const FString& Description);
    void Modify(UObject* Object);
    void Cancel();

private:
    FScopedTransaction Transaction;
};

/**
 * Common utilities for UnrealMCP commands
 */
class UNREALMCP_API FUnrealMCPCommonUtils
{
public:
    // JSON utilities
    static TSharedPtr<FJsonObject> CreateErrorResponse(const FString& Message);
    static TSharedPtr<FJsonObject> CreateSuccessResponse(const TSharedPtr<FJsonObject>& Data = nullptr);
    static void GetIntArrayFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, TArray<int32>& OutArray);
    static void GetFloatArrayFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, TArray<float>& OutArray);
    static FVector2D GetVector2DFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName);
    static FVector GetVectorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName);
    static FRotator GetRotatorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName);
    
    // Actor utilities
    static TSharedPtr<FJsonValue> ActorToJson(AActor* Actor);
    static TSharedPtr<FJsonObject> ActorToJsonObject(AActor* Actor, bool bDetailed = false);
    
    // Blueprint utilities
    static UBlueprint* FindBlueprint(const FString& BlueprintName);
    static UBlueprint* FindBlueprint(const FString& BlueprintName, FString& OutError);
    static UBlueprint* FindBlueprintByName(const FString& BlueprintName);
    static UClass* ResolveClass(const FString& NameOrPath, FString& OutError);
    static UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& GraphName, bool& bOutCollapsed);
    static UEdGraph* ResolveGraph(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Params, FString& OutError);
    static UEdGraph* FindOrCreateEventGraph(UBlueprint* Blueprint);
    static bool BuildPinType(const FString& TypeName, const FString& SubType, const FString& Container, FEdGraphPinType& OutType, FString& OutError);
    
    // Blueprint node utilities
    static UK2Node_Event* CreateEventNode(UEdGraph* Graph, const FString& EventName, const FVector2D& Position);
    static UK2Node_CallFunction* CreateFunctionCallNode(UEdGraph* Graph, UFunction* Function, const FVector2D& Position);
    static UK2Node_VariableGet* CreateVariableGetNode(UEdGraph* Graph, UBlueprint* Blueprint, const FString& VariableName, const FVector2D& Position);
    static UK2Node_VariableSet* CreateVariableSetNode(UEdGraph* Graph, UBlueprint* Blueprint, const FString& VariableName, const FVector2D& Position);
    static UK2Node_InputAction* CreateInputActionNode(UEdGraph* Graph, const FString& ActionName, const FVector2D& Position);
    static UK2Node_Self* CreateSelfReferenceNode(UEdGraph* Graph, const FVector2D& Position);
    static bool ConnectGraphNodes(UEdGraph* Graph, UEdGraphNode* SourceNode, const FString& SourcePinName, 
                                UEdGraphNode* TargetNode, const FString& TargetPinName);
    static UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction = EGPD_MAX);
    static UK2Node_Event* FindExistingEventNode(UEdGraph* Graph, const FString& EventName);

    // Property utilities
    static bool SetObjectProperty(UObject* Object, const FString& PropertyName,
                                 const TSharedPtr<FJsonValue>& Value, FString& OutErrorMessage);

    // World / actor lookup
    /** Mundo del editor. Con bPreferPIE devuelve el mundo de PIE si está corriendo. */
    static UWorld* GetEditorWorld(bool bPreferPIE = false);
    /** Busca por etiqueta del editor, luego por nombre interno, luego por ruta. */
    static AActor* FindActor(const FString& NameOrLabel, FString& OutError, UWorld* World = nullptr);
    /** Busca un componente por nombre (o clase) dentro del actor. */
    static UActorComponent* FindComponent(AActor* Actor, const FString& ComponentName);
    /**
     * Objeto objetivo a partir de params: "asset_path", o "actor" (alias "name") con "component" opcional.
     * Con "pie": true busca el actor en el mundo de PIE.
     */
    static UObject* ResolveTargetObject(const TSharedPtr<FJsonObject>& Params, FString& OutError);
    static FString GetActorDisplayName(const AActor* Actor);

    // Generic reflection
    /**
     * Resuelve rutas como "RelativeLocation.X", "Tags[0]" o "StaticMeshComponent.StaticMesh".
     * OutOwner es el UObject que contiene la propiedad final (para Modify/PostEditChange).
     */
    static bool ResolvePropertyPath(UObject* Object, const FString& Path, FProperty*& OutProperty, void*& OutValuePtr,
                                    UObject*& OutOwner, FString& OutError);
    static TSharedPtr<FJsonValue> PropertyValueToJson(FProperty* Property, const void* ValuePtr);
    static bool SetPropertyValueFromJson(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& Value, FString& OutError);
    /** Lista propiedades con tipo y valor. Filter es una subcadena del nombre (vacío = todas). */
    static TSharedPtr<FJsonObject> ObjectPropertiesToJson(UObject* Object, const FString& Filter, bool bEditableOnly, int32 MaxProperties = 400);
    /** Asigna una propiedad por ruta, con Modify y Pre/PostEditChange. El llamador abre la transacción. */
    static bool SetObjectPropertyByPath(UObject* Object, const FString& Path, const TSharedPtr<FJsonValue>& Value, FString& OutError);

    // Asset utilities
    /** Acepta /Game/Path/Asset, /Game/Path/Asset.Asset o un nombre corto que se busca en el AssetRegistry. */
    static UObject* ResolveAsset(const FString& NameOrPath, FString& OutError, UClass* RequiredClass = nullptr);
    /** Une carpeta y nombre en un paquete: ("/Game/UI", "WBP_Menu") -> "/Game/UI/WBP_Menu". */
    static FString MakePackagePath(const FString& Folder, const FString& AssetName);
    static TArray<TSharedPtr<FJsonValue>> StringArrayToJson(const TArray<FString>& Values);
}; 