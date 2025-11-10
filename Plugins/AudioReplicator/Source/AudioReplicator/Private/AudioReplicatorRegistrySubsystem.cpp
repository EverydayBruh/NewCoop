#include "AudioReplicatorRegistrySubsystem.h"
#include "AudioReplicatorComponent.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"

UAudioReplicatorRegistrySubsystem::UAudioReplicatorRegistrySubsystem()
{
}

void UAudioReplicatorRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    if (UWorld* World = GetWorld())
    {
        ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
            FOnActorSpawned::FDelegate::CreateUObject(this, &UAudioReplicatorRegistrySubsystem::HandleActorSpawned));
        GameStateSetHandle = World->GameStateSetEvent.AddUObject(this, &UAudioReplicatorRegistrySubsystem::HandleGameStateSet);

        if (AGameStateBase* GameState = World->GetGameState())
        {
            BindToGameState(GameState);
        }
    }
}

void UAudioReplicatorRegistrySubsystem::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        if (ActorSpawnedHandle.IsValid())
        {
            World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
            ActorSpawnedHandle.Reset();
        }
        if (GameStateSetHandle.IsValid())
        {
            World->GameStateSetEvent.Remove(GameStateSetHandle);
            GameStateSetHandle.Reset();
        }
    }

    // AGameStateBase does not expose PlayerState add/remove delegates in UE 5.6.
    CachedGameState.Reset();
    ReplicatorOwners.Empty();
    ChannelSubscriptions.Empty();
    PlayerSubscriptions.Empty();
    LastSessionSenders.Empty();

    Super::Deinitialize();
}

void UAudioReplicatorRegistrySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    if (AGameStateBase* GameState = InWorld.GetGameState())
    {
        BindToGameState(GameState);
    }
}

void UAudioReplicatorRegistrySubsystem::SubscribeToChannel_BP(const FGuid& SessionId, const FOnAudioReplicatorAvailable& Callback)
{
    if (!Callback.IsBound())
    {
        return;
    }

    TArray<FReplicatorSubscription>& List = ChannelSubscriptions.FindOrAdd(SessionId);
    FReplicatorSubscription& Subscription = List.AddDefaulted_GetRef();
    Subscription.Callback = Callback;
    Subscription.Listener = const_cast<UObject*>(Callback.GetUObject());
    Subscription.LastSessionId = SessionId;

    if (UAudioReplicatorComponent* Existing = GetLastSenderForSession_BP(SessionId))
    {
        Subscription.LastReplicator = Existing;
        Callback.ExecuteIfBound(Existing, SessionId);
    }
}

void UAudioReplicatorRegistrySubsystem::SubscribeToPlayer_BP(APlayerState* PlayerState, const FOnAudioReplicatorAvailable& Callback)
{
    if (!PlayerState || !Callback.IsBound())
    {
        return;
    }

    const TWeakObjectPtr<APlayerState> Key(PlayerState);
    TArray<FReplicatorSubscription>& List = PlayerSubscriptions.FindOrAdd(Key);
    FReplicatorSubscription& Subscription = List.AddDefaulted_GetRef();
    Subscription.Callback = Callback;
    Subscription.Listener = const_cast<UObject*>(Callback.GetUObject());
    Subscription.LastSessionId.Invalidate();

    if (UAudioReplicatorComponent* Existing = FindReplicatorForPlayer(PlayerState))
    {
        Subscription.LastReplicator = Existing;
        Callback.ExecuteIfBound(Existing, Subscription.LastSessionId);
    }
}

void UAudioReplicatorRegistrySubsystem::Unsubscribe_BP(const FGuid& SessionId, UObject* Listener)
{
    if (!Listener)
    {
        return;
    }

    if (TArray<FReplicatorSubscription>* List = ChannelSubscriptions.Find(SessionId))
    {
        for (int32 Index = List->Num() - 1; Index >= 0; --Index)
        {
            const FReplicatorSubscription& Subscription = (*List)[Index];
            if (!Subscription.IsValid() || Subscription.Listener.Get() == Listener)
            {
                List->RemoveAtSwap(Index);
            }
        }

        if (List->Num() == 0)
        {
            ChannelSubscriptions.Remove(SessionId);
        }
    }
}

void UAudioReplicatorRegistrySubsystem::UnsubscribeAllFor_BP(UObject* Listener)
{
    if (!Listener)
    {
        return;
    }

    const TWeakObjectPtr<UObject> ListenerPtr(Listener);

    for (auto It = ChannelSubscriptions.CreateIterator(); It; ++It)
    {
        TArray<FReplicatorSubscription>& Subs = It.Value();
        for (int32 Index = Subs.Num() - 1; Index >= 0; --Index)
        {
            if (!Subs[Index].IsValid() || Subs[Index].Listener == ListenerPtr)
            {
                Subs.RemoveAtSwap(Index);
            }
        }

        if (Subs.Num() == 0)
        {
            It.RemoveCurrent();
        }
    }

    for (auto It = PlayerSubscriptions.CreateIterator(); It; ++It)
    {
        TArray<FReplicatorSubscription>& Subs = It.Value();
        for (int32 Index = Subs.Num() - 1; Index >= 0; --Index)
        {
            if (!Subs[Index].IsValid() || Subs[Index].Listener == ListenerPtr)
            {
                Subs.RemoveAtSwap(Index);
            }
        }

        if (Subs.Num() == 0)
        {
            It.RemoveCurrent();
        }
    }
}

UAudioReplicatorComponent* UAudioReplicatorRegistrySubsystem::GetLastSenderForSession_BP(const FGuid& SessionId) const
{
    const_cast<UAudioReplicatorRegistrySubsystem*>(this)->CleanupExpiredSessionSenders();
    if (const TWeakObjectPtr<UAudioReplicatorComponent>* Found = LastSessionSenders.Find(SessionId))
    {
        return Found->Get();
    }
    return nullptr;
}

UAudioReplicatorComponent* UAudioReplicatorRegistrySubsystem::GetLocalReplicator_BP() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    auto ResolveFromPlayerState = [this](APlayerState* PlayerState) -> UAudioReplicatorComponent*
    {
        if (!PlayerState)
        {
            return nullptr;
        }

        if (UAudioReplicatorComponent* Registered = FindReplicatorForPlayer(PlayerState))
        {
            return Registered;
        }

        return PlayerState->FindComponentByClass<UAudioReplicatorComponent>();
    };

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* Controller = It->Get();
        if (!Controller || !Controller->IsLocalController())
        {
            continue;
        }

        if (UAudioReplicatorComponent* FromState = ResolveFromPlayerState(Controller->PlayerState))
        {
            return FromState;
        }

        if (const APawn* Pawn = Controller->GetPawn())
        {
            if (UAudioReplicatorComponent* PawnComponent = Pawn->FindComponentByClass<UAudioReplicatorComponent>())
            {
                return PawnComponent;
            }
        }

        if (UAudioReplicatorComponent* ControllerComponent = Controller->FindComponentByClass<UAudioReplicatorComponent>())
        {
            return ControllerComponent;
        }
    }

    for (const auto& Pair : ReplicatorOwners)
    {
        UAudioReplicatorComponent* Component = Pair.Key.Get();
        if (!Component)
        {
            continue;
        }

        if (const AActor* Owner = Component->GetOwner())
        {
            if (const APlayerController* PC = Cast<APlayerController>(Owner))
            {
                if (PC->IsLocalController())
                {
                    return Component;
                }
            }
            else if (Owner->HasLocalNetOwner())
            {
                return Component;
            }
        }
    }

    return nullptr;
}

void UAudioReplicatorRegistrySubsystem::RegisterReplicator(UAudioReplicatorComponent* Component)
{
    if (!Component)
    {
        return;
    }

    CleanupExpiredSessionSenders();
    CleanupExpiredSubscriptions();

    const TWeakObjectPtr<UAudioReplicatorComponent> Key(Component);
    if (ReplicatorOwners.Contains(Key))
    {
        return;
    }

    APlayerState* PlayerState = nullptr;
    if (AActor* Owner = Component->GetOwner())
    {
        PlayerState = Cast<APlayerState>(Owner);
        if (!PlayerState)
        {
            if (const APlayerController* PC = Cast<APlayerController>(Owner))
            {
                PlayerState = PC->PlayerState;
            }
        }
    }
    if (!PlayerState)
    {
        PlayerState = Component->GetTypedOuter<APlayerState>();
    }
    ReplicatorOwners.Add(Key, PlayerState);

    OnReplicatorAdded.Broadcast(Component);

    if (PlayerState)
    {
        NotifyPlayerSubscribers(PlayerState, Component, FGuid());
    }
}

void UAudioReplicatorRegistrySubsystem::UnregisterReplicator(UAudioReplicatorComponent* Component)
{
    if (!Component)
    {
        return;
    }

    const TWeakObjectPtr<UAudioReplicatorComponent> Key(Component);
    if (ReplicatorOwners.Remove(Key) > 0)
    {
        for (auto It = LastSessionSenders.CreateIterator(); It; ++It)
        {
            if (It.Value().Get() == Component)
            {
                It.RemoveCurrent();
            }
        }

        OnReplicatorRemoved.Broadcast(Component);
    }
}

void UAudioReplicatorRegistrySubsystem::NotifySessionActivity(const FGuid& SessionId, UAudioReplicatorComponent* Component)
{
    if (!Component || !SessionId.IsValid())
    {
        return;
    }

    LastSessionSenders.Add(SessionId, Component);
    NotifyChannelSubscribers(SessionId, Component);

    const TWeakObjectPtr<UAudioReplicatorComponent> Key(Component);
    if (const TWeakObjectPtr<APlayerState>* Owner = ReplicatorOwners.Find(Key))
    {
        if (APlayerState* PlayerState = Owner->Get())
        {
            NotifyPlayerSubscribers(PlayerState, Component, SessionId);
        }
    }
}

void UAudioReplicatorRegistrySubsystem::HandleActorSpawned(AActor* Actor)
{
    if (APlayerState* PlayerState = Cast<APlayerState>(Actor))
    {
        RegisterFromPlayerState(PlayerState);
    }
}

void UAudioReplicatorRegistrySubsystem::HandleGameStateSet(AGameStateBase* GameState)
{
    BindToGameState(GameState);
}

void UAudioReplicatorRegistrySubsystem::BindToGameState(AGameStateBase* GameState)
{
    if (CachedGameState.Get() == GameState)
    {
        return;
    }

    CachedGameState = GameState;
    if (GameState)
    {
        RefreshFromGameState(GameState);
    }
}

void UAudioReplicatorRegistrySubsystem::RefreshFromGameState(AGameStateBase* GameState)
{
    if (!GameState)
    {
        return;
    }

    for (APlayerState* PlayerState : GameState->PlayerArray)
    {
        RegisterFromPlayerState(PlayerState);
    }
}

void UAudioReplicatorRegistrySubsystem::RegisterFromPlayerState(APlayerState* PlayerState)
{
    if (!PlayerState)
    {
        return;
    }

    if (UAudioReplicatorComponent* Component = PlayerState->FindComponentByClass<UAudioReplicatorComponent>())
    {
        RegisterReplicator(Component);
        // OnEndPlay is a dynamic multicast delegate; bind via AddUniqueDynamic.
        PlayerState->OnEndPlay.AddUniqueDynamic(this, &UAudioReplicatorRegistrySubsystem::HandlePlayerStateEndPlay);
    }
}

void UAudioReplicatorRegistrySubsystem::HandlePlayerStateEndPlay(AActor* Actor, EEndPlayReason::Type)
{
    if (APlayerState* PS = Cast<APlayerState>(Actor))
    {
        if (UAudioReplicatorComponent* Comp = PS->FindComponentByClass<UAudioReplicatorComponent>())
        {
            UnregisterReplicator(Comp);
        }
        PlayerSubscriptions.Remove(PS);
    }
}


void UAudioReplicatorRegistrySubsystem::NotifyChannelSubscribers(const FGuid& SessionId, UAudioReplicatorComponent* Component)
{
    if (TArray<FReplicatorSubscription>* List = ChannelSubscriptions.Find(SessionId))
    {
        for (int32 Index = List->Num() - 1; Index >= 0; --Index)
        {
            FReplicatorSubscription& Subscription = (*List)[Index];
            if (!Subscription.IsValid())
            {
                List->RemoveAtSwap(Index);
                continue;
            }

            Subscription.LastReplicator = Component;
            Subscription.LastSessionId = SessionId;
            Subscription.Callback.ExecuteIfBound(Component, SessionId);
        }

        if (List->Num() == 0)
        {
            ChannelSubscriptions.Remove(SessionId);
        }
    }
}

void UAudioReplicatorRegistrySubsystem::NotifyPlayerSubscribers(APlayerState* PlayerState, UAudioReplicatorComponent* Component, const FGuid& SessionId)
{
    const TWeakObjectPtr<APlayerState> Key(PlayerState);
    if (TArray<FReplicatorSubscription>* List = PlayerSubscriptions.Find(Key))
    {
        for (int32 Index = List->Num() - 1; Index >= 0; --Index)
        {
            FReplicatorSubscription& Subscription = (*List)[Index];
            if (!Subscription.IsValid())
            {
                List->RemoveAtSwap(Index);
                continue;
            }

            Subscription.LastReplicator = Component;
            if (SessionId.IsValid())
            {
                Subscription.LastSessionId = SessionId;
            }
            Subscription.Callback.ExecuteIfBound(Component, Subscription.LastSessionId);
        }

        if (List->Num() == 0)
        {
            PlayerSubscriptions.Remove(Key);
        }
    }
}

void UAudioReplicatorRegistrySubsystem::CleanupExpiredSubscriptions()
{
    for (auto It = ChannelSubscriptions.CreateIterator(); It; ++It)
    {
        TArray<FReplicatorSubscription>& Subs = It.Value();
        for (int32 Index = Subs.Num() - 1; Index >= 0; --Index)
        {
            if (!Subs[Index].IsValid())
            {
                Subs.RemoveAtSwap(Index);
            }
        }

        if (Subs.Num() == 0)
        {
            It.RemoveCurrent();
        }
    }

    for (auto It = PlayerSubscriptions.CreateIterator(); It; ++It)
    {
        TArray<FReplicatorSubscription>& Subs = It.Value();
        for (int32 Index = Subs.Num() - 1; Index >= 0; --Index)
        {
            if (!Subs[Index].IsValid())
            {
                Subs.RemoveAtSwap(Index);
            }
        }

        if (Subs.Num() == 0)
        {
            It.RemoveCurrent();
        }
    }
}

void UAudioReplicatorRegistrySubsystem::CleanupExpiredSessionSenders()
{
    for (auto It = LastSessionSenders.CreateIterator(); It; ++It)
    {
        if (!It.Value().IsValid())
        {
            It.RemoveCurrent();
        }
    }
}

UAudioReplicatorComponent* UAudioReplicatorRegistrySubsystem::FindReplicatorForPlayer(APlayerState* PlayerState) const
{
    if (!PlayerState)
    {
        return nullptr;
    }

    for (const auto& Pair : ReplicatorOwners)
    {
        if (Pair.Value.Get() == PlayerState)
        {
            return Pair.Key.Get();
        }
    }

    return nullptr;
}
