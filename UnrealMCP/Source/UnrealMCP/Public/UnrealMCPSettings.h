#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UnrealMCPSettings.generated.h"

/**
 * Project Settings > Plugins > Unreal MCP
 */
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Unreal MCP"))
class UNREALMCP_API UUnrealMCPSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UUnrealMCPSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Inicia el servidor TCP al abrir el editor. */
	UPROPERTY(Config, EditAnywhere, Category = "Server")
	bool bAutoStart = true;

	/** Solo se recomienda 127.0.0.1: el servidor no tiene autenticación. */
	UPROPERTY(Config, EditAnywhere, Category = "Server")
	FString Host = TEXT("127.0.0.1");

	UPROPERTY(Config, EditAnywhere, Category = "Server", meta = (ClampMin = "1024", ClampMax = "65535"))
	int32 Port = 55557;

	/** Segundos que se espera al game thread antes de devolver un error de timeout. */
	UPROPERTY(Config, EditAnywhere, Category = "Server", meta = (ClampMin = "1", ClampMax = "600"))
	float CommandTimeoutSeconds = 60.f;

	/** Escribe en el log cada comando recibido. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bVerboseLogging = false;
};
