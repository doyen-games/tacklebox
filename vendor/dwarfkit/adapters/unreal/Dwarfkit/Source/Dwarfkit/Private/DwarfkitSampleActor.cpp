#include "DwarfkitSampleActor.h"

#include "DwarfkitAsyncActions.h"

void ADwarfkitSampleActor::BeginPlay()
{
    Super::BeginPlay();

    UDwarfkitSubsystem* Subsystem = GetGameInstance()->GetSubsystem<UDwarfkitSubsystem>();
    Subsystem->Configure(AppName, ChainId, ApiUrl);

    UDkLoginAction* LoginAction = UDkLoginAction::Login(this, LoginActor, LoginPermission);
    LoginAction->Completed.AddDynamic(this, &ADwarfkitSampleActor::OnLoggedIn);
    LoginAction->Activate();
}

void ADwarfkitSampleActor::OnLoggedIn(const FDkSessionInfo& Session, const FString& Error)
{
    if (!Error.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("Dwarfkit login failed: %s"), *Error);
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("Dwarfkit logged in as %s@%s"), *Session.Actor,
           *Session.Permission);

    const FString ActionJson = FString::Printf(
        TEXT("{\"account\":\"eosio.token\",\"name\":\"transfer\","
             "\"authorization\":[{\"actor\":\"%s\",\"permission\":\"%s\"}],"
             "\"data\":{\"from\":\"%s\",\"to\":\"%s\",\"quantity\":\"%s\","
             "\"memo\":\"sent with dwarfkit\"}}"),
        *Session.Actor, *Session.Permission, *Session.Actor, *TransferTo, *Quantity);

    UDkTransactAction* TransactAction = UDkTransactAction::Transact(this, ActionJson, true);
    TransactAction->Completed.AddDynamic(this, &ADwarfkitSampleActor::OnTransacted);
    TransactAction->Activate();
}

void ADwarfkitSampleActor::OnTransacted(const FString& TransactionId, const FString& Error)
{
    if (!Error.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("Dwarfkit transact failed: %s"), *Error);
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("Dwarfkit transaction broadcast: %s"), *TransactionId);
}
