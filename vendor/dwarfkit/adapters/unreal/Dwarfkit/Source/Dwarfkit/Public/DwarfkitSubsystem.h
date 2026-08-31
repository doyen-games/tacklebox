// GameInstance subsystem owning the Dwarfkit SessionKit (BLUEPRINT.md 8.1).
// All kit calls run on a background thread; results come back through the
// async action delegates on the game thread.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

THIRD_PARTY_INCLUDES_START
#include <dwarfkit/plugins/wallet/anchor.hpp>
#include <dwarfkit/session.hpp>
THIRD_PARTY_INCLUDES_END

#include "DwarfkitUI.h"

#include "DwarfkitSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FDkSessionInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Dwarfkit")
    FString ChainId;

    UPROPERTY(BlueprintReadOnly, Category = "Dwarfkit")
    FString Actor;

    UPROPERTY(BlueprintReadOnly, Category = "Dwarfkit")
    FString Permission;
};

UCLASS()
class DWARFKIT_API UDwarfkitSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Configure the kit for a chain before logging in. ChainId is the 64-char
    // hex id; Url the API node.
    UFUNCTION(BlueprintCallable, Category = "Dwarfkit")
    void Configure(const FString& AppName, const FString& ChainId, const FString& Url);

    // Assign the Blueprint UI receiving prompts.
    UFUNCTION(BlueprintCallable, Category = "Dwarfkit")
    void SetUI(UDwarfkitUI* InUI);

    UFUNCTION(BlueprintPure, Category = "Dwarfkit")
    bool HasSession() const;

    UFUNCTION(BlueprintPure, Category = "Dwarfkit")
    FDkSessionInfo GetSessionInfo() const;

    // Native access for the async actions.
    dwarfkit::SessionKit* GetKit() const { return Kit.Get(); }
    std::shared_ptr<dwarfkit::Session> GetSession() const { return Session; }
    void SetSession(std::shared_ptr<dwarfkit::Session> InSession) { Session = InSession; }
    TWeakObjectPtr<UDwarfkitUI> GetUI() const { return UI; }

private:
    TUniquePtr<dwarfkit::SessionKit> Kit;
    std::shared_ptr<dwarfkit::Session> Session;
    TWeakObjectPtr<UDwarfkitUI> UI;
};
