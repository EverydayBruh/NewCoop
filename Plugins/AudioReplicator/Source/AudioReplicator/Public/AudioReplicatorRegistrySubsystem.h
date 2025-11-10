#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/EngineTypes.h" // EEndPlayReason::Type
#include "AudioReplicatorRegistrySubsystem.generated.h"

class AActor;
class AGameStateBase;
class APlayerState;
class UAudioReplicatorComponent;

// Delegate invoked when a replicator is available for a subscription.
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnAudioReplicatorAvailable, UAudioReplicatorComponent*, Replicator, FGuid, SessionId);

// Registry events for tracking replicator lifecycle.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAudioReplicatorRegistryChanged, UAudioReplicatorComponent*, Replicator);

/**
 * UAudioReplicatorRegistrySubsystem keeps an inventory of all AudioReplicator components in the world.
 * It supports push registration from components and pull discovery via PlayerState traversal.
 */
UCLASS()
class AUDIOREPLICATOR_API UAudioReplicatorRegistrySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    UAudioReplicatorRegistrySubsystem();

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

    /** Subscribe to updates for a specific Opus session identifier. */
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Registry")
    void SubscribeToChannel_BP(const FGuid& SessionId, const FOnAudioReplicatorAvailable& Callback);

    /** Subscribe to replicator availability originating from a given player state. */
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Registry")
    void SubscribeToPlayer_BP(APlayerState* PlayerState, const FOnAudioReplicatorAvailable& Callback);

    /** Remove a subscription for a specific session and listener. */
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Registry")
    void Unsubscribe_BP(const FGuid& SessionId, UObject* Listener);

    /** Remove every subscription owned by the provided listener object. */
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Registry")
    void UnsubscribeAllFor_BP(UObject* Listener);

    /** Return the last known sender replicator for a given session. */
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Registry")
    UAudioReplicatorComponent* GetLastSenderForSession_BP(const FGuid& SessionId) const;

    /** Resolve the replicator component owned by the local player (if any). */
    UFUNCTION(BlueprintCallable, Category = "AudioReplicator|Registry")
    UAudioReplicatorComponent* GetLocalReplicator_BP() const;

    /** Fired when a replicator enters the registry. */
    UPROPERTY(BlueprintAssignable, Category = "AudioReplicator|Registry")
    FOnAudioReplicatorRegistryChanged OnReplicatorAdded;

    /** Fired when a replicator leaves the registry. */
    UPROPERTY(BlueprintAssignable, Category = "AudioReplicator|Registry")
    FOnAudioReplicatorRegistryChanged OnReplicatorRemoved;

    /** Push-registration entry point called by the component itself. */
    void RegisterReplicator(UAudioReplicatorComponent* Component);

    /** Unregister a component from the live index. */
    void UnregisterReplicator(UAudioReplicatorComponent* Component);

    /** Record activity for a session so subscribers can resolve the source component. */
    void NotifySessionActivity(const FGuid& SessionId, UAudioReplicatorComponent* Component);

private:
    struct FReplicatorSubscription
    {
        FOnAudioReplicatorAvailable Callback;
        TWeakObjectPtr<UObject> Listener;
        TWeakObjectPtr<UAudioReplicatorComponent> LastReplicator;
        FGuid LastSessionId;

        bool IsValid() const { return Listener.IsValid() && Callback.IsBound(); }
    };

    void HandleActorSpawned(AActor* Actor);
    void HandleGameStateSet(AGameStateBase* GameState);

    // PlayerState lifecycle is tracked via OnEndPlay instead of GameState delegates.
    UFUNCTION()
    void HandlePlayerStateEndPlay(AActor* Actor, EEndPlayReason::Type Reason);


    void BindToGameState(AGameStateBase* GameState);
    void RefreshFromGameState(AGameStateBase* GameState);
    void RegisterFromPlayerState(APlayerState* PlayerState);

    void NotifyChannelSubscribers(const FGuid& SessionId, UAudioReplicatorComponent* Component);
    void NotifyPlayerSubscribers(APlayerState* PlayerState, UAudioReplicatorComponent* Component, const FGuid& SessionId);

    void CleanupExpiredSubscriptions();
    void CleanupExpiredSessionSenders();

    UAudioReplicatorComponent* FindReplicatorForPlayer(APlayerState* PlayerState) const;

    TMap<TWeakObjectPtr<UAudioReplicatorComponent>, TWeakObjectPtr<APlayerState>> ReplicatorOwners;
    TMap<FGuid, TArray<FReplicatorSubscription>> ChannelSubscriptions;
    TMap<TWeakObjectPtr<APlayerState>, TArray<FReplicatorSubscription>> PlayerSubscriptions;
    TMap<FGuid, TWeakObjectPtr<UAudioReplicatorComponent>> LastSessionSenders;

    FDelegateHandle ActorSpawnedHandle;
    FDelegateHandle GameStateSetHandle;
    TWeakObjectPtr<AGameStateBase> CachedGameState;
};
