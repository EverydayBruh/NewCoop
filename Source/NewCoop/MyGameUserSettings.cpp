// Fill out your copyright notice in the Description page of Project Settings.


#include "MyGameUserSettings.h"

UMyGameUserSettings::UMyGameUserSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MasterVolume = 1.0f;
	SFXVolume = 1.0f;
	MusicVolume = 1.0f;
}

void UMyGameUserSettings::SetMasterVolume(float Volume)
{
	MasterVolume = Volume;
}

void UMyGameUserSettings::SetSFXVolume(float Volume)
{
	SFXVolume = Volume;
}

void UMyGameUserSettings::SetMusicVolume(float Volume)
{
	MusicVolume = Volume;
}

void UMyGameUserSettings::SetMicThresholdValue(float Value)
{
	MicThresholdValue = Value;
}

void UMyGameUserSettings::SetMicVolume(float Volume)
{
	MicVolume = Volume;
}
