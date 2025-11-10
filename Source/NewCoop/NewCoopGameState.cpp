// Fill out your copyright notice in the Description page of Project Settings.


#include "NewCoopGameState.h"

// NewCoopGameState.cpp
void ANewCoopGameState::BeginPlay()
{
    Super::BeginPlay();

    // Инициализируем кеш тем, что уже есть (важно при позднем присоединении)
    for (APlayerState* PS : PlayerArray)
    {
        if (PS && !KnownPS.Contains(PS))
        {
            KnownPS.Add(PS);
            OnPlayerStateAddedBP.Broadcast(PS);
        }
    }
}

void ANewCoopGameState::AddPlayerState(APlayerState* PS)
{
    Super::AddPlayerState(PS);

    if (PS && !KnownPS.Contains(PS))
    {
        KnownPS.Add(PS);
        OnPlayerStateAddedBP.Broadcast(PS); // сработает и на клиентах, когда репликация дошла
    }
}

void ANewCoopGameState::RemovePlayerState(APlayerState* PS)
{
    Super::RemovePlayerState(PS);

    if (PS && KnownPS.Contains(PS))
    {
        OnPlayerStateRemovedBP.Broadcast(PS);
        KnownPS.Remove(PS);
    }
}
