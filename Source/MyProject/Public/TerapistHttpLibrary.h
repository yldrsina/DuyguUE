// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TerapistHttpLibrary.generated.h"

/**
 * 
 */
UCLASS()
class MYPROJECT_API UTerapistHttpLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable)
	static TArray<uint8> StringToByteArray(const FString& InString);

	UFUNCTION(BlueprintCallable)
	static void WriteBytesToFile(const FString FileName, const TArray<uint8>& InBytes);
	
	
	
};
