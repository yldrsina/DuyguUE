// Fill out your copyright notice in the Description page of Project Settings.


#include "TerapistHttpLibrary.h"

TArray<uint8> UTerapistHttpLibrary::StringToByteArray(const FString& InString)
{
	TArray<uint8> ReturnArray;
	ReturnArray.AddUninitialized(InString.Len() * sizeof(FString::ElementType));
	StringToBytes(InString, ReturnArray.GetData(), ReturnArray.Num());
	return ReturnArray;
}

void UTerapistHttpLibrary::WriteBytesToFile(const FString FileName, const TArray<uint8>& InBytes)
{
	FFileHelper::SaveArrayToFile(InBytes, *FileName);
}
