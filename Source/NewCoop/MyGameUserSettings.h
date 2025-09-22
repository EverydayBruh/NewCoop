// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "MyGameUserSettings.generated.h"

/**
 * 
 */
UCLASS(config = GameUserSettings, configdonotcheckdefaults, Blueprintable)
class NEWCOOP_API UMyGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()
	
public:
	UMyGameUserSettings(const FObjectInitializer& ObjectInitializer);

	/** Returns the user setting for game master volume. */
	UFUNCTION(BlueprintPure, Category = Settings)
	float GetMasterVolume() const { return MasterVolume; }

	/** Sets the user setting for game master volume. */
	UFUNCTION(BlueprintCallable, Category = Settings)
	void SetMasterVolume(float Volume);

	/** Returns the user setting for game SFX volume. */
	UFUNCTION(BlueprintPure, Category = Settings)
	float GetSFXVolume() const { return SFXVolume; }

	/** Sets the user setting for game SFX volume. */
	UFUNCTION(BlueprintCallable, Category = Settings)
	void SetSFXVolume(float Volume);

	/** Returns the user setting for game music volume. */
	UFUNCTION(BlueprintPure, Category = Settings)
	float GetMusicVolume() const { return MusicVolume; }

	/** Sets the user setting for game music volume. */
	UFUNCTION(BlueprintCallable, Category = Settings)
	void SetMusicVolume(float Volume);

	/** Returns the user setting for user's microphone threshold value. */
	UFUNCTION(BlueprintPure, Category = Settings)
	float GetMicThresholdValue() const { return MicThresholdValue; }

	/** Sets the user setting for user's microphone threshold value. */
	UFUNCTION(BlueprintCallable, Category = Settings)
	void SetMicThresholdValue(float Value);

	/** Returns the user setting for user's microphone volume. */
	UFUNCTION(BlueprintPure, Category = Settings)
	float GetMicVolume() const { return MicVolume; }

	/** Sets the user setting for user's microphone volume. */
	UFUNCTION(BlueprintCallable, Category = Settings)
	void SetMicVolume(float Volume);

protected:

    UPROPERTY(config)
    float MasterVolume;

	UPROPERTY(config)
	float SFXVolume;

	UPROPERTY(config)
	float MusicVolume;

	UPROPERTY(config)
	float MicThresholdValue;

	UPROPERTY(config)
	float MicVolume;

};
