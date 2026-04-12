// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "MyGameUserSettings.generated.h"

/**
 * 
 */
UCLASS(config= GameUserSettings, configdonotcheckdefaults, Blueprintable)
class MYPROJECT_API UMyGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()
	public:
	UPROPERTY(config)
	FString DuyguTerapistIP;

	UFUNCTION(BlueprintCallable, Category = "Settings")
	void GetDuyguTerapistIP(FString& OutIP) const
	{
		OutIP = DuyguTerapistIP;
	}
};
