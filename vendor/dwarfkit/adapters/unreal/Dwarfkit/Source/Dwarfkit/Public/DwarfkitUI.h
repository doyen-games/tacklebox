// Blueprint-implementable user interface for Dwarfkit prompts (BLUEPRINT.md
// 8.1). Kit workers forward UserInterface calls here, dispatched to the game
// thread and awaited with an FEvent.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "DwarfkitUI.generated.h"

USTRUCT(BlueprintType)
struct FDkPromptElement
{
    GENERATED_BODY()

    // "qr", "link", "button", "countdown", "textarea", ...
    UPROPERTY(BlueprintReadOnly, Category = "Dwarfkit")
    FString Type;

    UPROPERTY(BlueprintReadOnly, Category = "Dwarfkit")
    FString Label;

    // The element's data payload as JSON text (e.g. the esr: URI for a qr).
    UPROPERTY(BlueprintReadOnly, Category = "Dwarfkit")
    FString DataJson;
};

// Subclass in Blueprint (or C++) and assign to UDwarfkitSubsystem. Events fire
// on the game thread; the kit worker waits until the corresponding call
// returns.
UCLASS(Blueprintable, BlueprintType)
class DWARFKIT_API UDwarfkitUI : public UObject
{
    GENERATED_BODY()

public:
    // Show a prompt (login QR, transaction countdown, ...). Non-blocking:
    // display and return, like ConsoleUserInterface.
    UFUNCTION(BlueprintImplementableEvent, Category = "Dwarfkit")
    void OnPrompt(const FString& Title, const FString& Body,
                  const TArray<FDkPromptElement>& Elements);

    // Progress updates from transact plugins.
    UFUNCTION(BlueprintImplementableEvent, Category = "Dwarfkit")
    void OnStatus(const FString& Message);

    // An error surfaced by the kit.
    UFUNCTION(BlueprintImplementableEvent, Category = "Dwarfkit")
    void OnErrorMessage(const FString& Message);
};
