// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameState.h"
#include "NewCoopGameState.generated.h"

/**
 * 
 */
UCLASS()
class NEWCOOP_API ANewCoopGameState : public AGameState
{
	GENERATED_BODY()
    public:
        DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPSAdded, APlayerState*, PlayerState);
        DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPSRemoved, APlayerState*, PlayerState);

        UPROPERTY(BlueprintAssignable, Category = "Players")
        FOnPSAdded OnPlayerStateAddedBP;

        UPROPERTY(BlueprintAssignable, Category = "Players")
        FOnPSRemoved OnPlayerStateRemovedBP;

    protected:
        virtual void BeginPlay() override;
        virtual void AddPlayerState(APlayerState* PS) override;
        virtual void RemovePlayerState(APlayerState* PS) override;

    private:
        // Локальный кеш, чтобы при позднем джойне отстрелить «инициальные» PS
        UPROPERTY()
        TSet<TObjectPtr<APlayerState>> KnownPS;
};
