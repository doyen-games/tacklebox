// Blueprint async nodes for Login, Transact and Restore (BLUEPRINT.md 8.1).
// Each node runs the kit call on a background thread and fires its pins on
// the game thread.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "DwarfkitSubsystem.h"

#include "DwarfkitAsyncActions.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDkLoginResult, const FDkSessionInfo&, Session,
                                             const FString&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDkTransactResult, const FString&, TransactionId,
                                             const FString&, Error);

UCLASS()
class DWARFKIT_API UDkLoginAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    // Log in with the configured wallet plugin. Actor/Permission may be empty
    // to let the wallet decide.
    UFUNCTION(BlueprintCallable, Category = "Dwarfkit",
              meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
    static UDkLoginAction* Login(UObject* WorldContextObject, const FString& Actor,
                                 const FString& Permission);

    virtual void Activate() override;

    UPROPERTY(BlueprintAssignable)
    FDkLoginResult Completed;

    UPROPERTY()
    TObjectPtr<UDwarfkitSubsystem> Subsystem;
    FString Actor;
    FString Permission;
};

UCLASS()
class DWARFKIT_API UDkRestoreAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    // Restore the persisted session, if any.
    UFUNCTION(BlueprintCallable, Category = "Dwarfkit",
              meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
    static UDkRestoreAction* Restore(UObject* WorldContextObject);

    virtual void Activate() override;

    UPROPERTY(BlueprintAssignable)
    FDkLoginResult Completed;

    UPROPERTY()
    TObjectPtr<UDwarfkitSubsystem> Subsystem;
};

UCLASS()
class DWARFKIT_API UDkTransactAction : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    // Sign (and broadcast) one action given as JSON:
    // {"account": ..., "name": ..., "authorization": [...], "data": {...}}
    UFUNCTION(BlueprintCallable, Category = "Dwarfkit",
              meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
    static UDkTransactAction* Transact(UObject* WorldContextObject, const FString& ActionJson,
                                       bool bBroadcast = true);

    virtual void Activate() override;

    UPROPERTY(BlueprintAssignable)
    FDkTransactResult Completed;

    UPROPERTY()
    TObjectPtr<UDwarfkitSubsystem> Subsystem;
    FString ActionJson;
    bool bBroadcast = true;
};
