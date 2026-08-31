// Sample actor for the login + transfer map (BLUEPRINT.md Phase 7): place it
// in a level, set Actor/Permission (or leave them empty and assign a
// UDwarfkitUI widget for a QR login) and it logs in on BeginPlay, then sends
// a small eosio.token transfer.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "DwarfkitSubsystem.h"

#include "DwarfkitSampleActor.generated.h"

UCLASS()
class DWARFKIT_API ADwarfkitSampleActor : public AActor
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;

    UPROPERTY(EditAnywhere, Category = "Dwarfkit")
    FString AppName = TEXT("dwarfkit-sample");

    // Jungle 4 by default
    UPROPERTY(EditAnywhere, Category = "Dwarfkit")
    FString ChainId =
        TEXT("73e4385a2708e6d7048834fbc1079f2fabb17b3c125b146af438971e90716c4d");

    UPROPERTY(EditAnywhere, Category = "Dwarfkit")
    FString ApiUrl = TEXT("https://jungle4.greymass.com");

    UPROPERTY(EditAnywhere, Category = "Dwarfkit")
    FString LoginActor;

    UPROPERTY(EditAnywhere, Category = "Dwarfkit")
    FString LoginPermission = TEXT("active");

    UPROPERTY(EditAnywhere, Category = "Dwarfkit")
    FString TransferTo = TEXT("teamgreymass");

    UPROPERTY(EditAnywhere, Category = "Dwarfkit")
    FString Quantity = TEXT("0.0001 EOS");

private:
    UFUNCTION()
    void OnLoggedIn(const FDkSessionInfo& Session, const FString& Error);

    UFUNCTION()
    void OnTransacted(const FString& TransactionId, const FString& Error);
};
