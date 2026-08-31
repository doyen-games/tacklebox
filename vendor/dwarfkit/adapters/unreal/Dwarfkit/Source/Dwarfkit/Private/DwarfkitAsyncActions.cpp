#include "DwarfkitAsyncActions.h"

#include "Async/Async.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace
{

UDwarfkitSubsystem* FindSubsystem(UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return nullptr;
    }
    const UWorld* World = WorldContextObject->GetWorld();
    if (!World || !World->GetGameInstance())
    {
        return nullptr;
    }
    return World->GetGameInstance()->GetSubsystem<UDwarfkitSubsystem>();
}

FDkSessionInfo InfoFor(const std::shared_ptr<dwarfkit::Session>& Session)
{
    FDkSessionInfo Info;
    if (Session)
    {
        Info.ChainId = UTF8_TO_TCHAR(Session->chain.id.hexString().c_str());
        Info.Actor = UTF8_TO_TCHAR(Session->actor().toString().c_str());
        Info.Permission = UTF8_TO_TCHAR(Session->permission().toString().c_str());
    }
    return Info;
}

}  // namespace

// ---- Login -----------------------------------------------------------------

UDkLoginAction* UDkLoginAction::Login(UObject* WorldContextObject, const FString& InActor,
                                      const FString& InPermission)
{
    UDkLoginAction* Action = NewObject<UDkLoginAction>();
    Action->Subsystem = FindSubsystem(WorldContextObject);
    Action->Actor = InActor;
    Action->Permission = InPermission;
    return Action;
}

void UDkLoginAction::Activate()
{
    if (!Subsystem || !Subsystem->GetKit())
    {
        Completed.Broadcast(FDkSessionInfo(), TEXT("Dwarfkit is not configured"));
        return;
    }
    UDwarfkitSubsystem* LocalSubsystem = Subsystem;
    const std::string ActorStr = TCHAR_TO_UTF8(*Actor);
    const std::string PermissionStr = TCHAR_TO_UTF8(*Permission);

    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
              [this, LocalSubsystem, ActorStr, PermissionStr]()
              {
                  dwarfkit::LoginOptions Options;
                  if (!ActorStr.empty())
                  {
                      Options.permissionLevel = dwarfkit::PermissionLevel{
                          dwarfkit::Name::from(ActorStr),
                          dwarfkit::Name::from(
                              PermissionStr.empty() ? "active" : PermissionStr)};
                  }
                  auto LoginResult = LocalSubsystem->GetKit()->login(Options);

                  AsyncTask(ENamedThreads::GameThread,
                            [this, LocalSubsystem,
                             LoginResult = MoveTemp(LoginResult)]() mutable
                            {
                                if (LoginResult)
                                {
                                    LocalSubsystem->SetSession(LoginResult->session);
                                    Completed.Broadcast(InfoFor(LoginResult->session),
                                                        FString());
                                }
                                else
                                {
                                    Completed.Broadcast(
                                        FDkSessionInfo(),
                                        UTF8_TO_TCHAR(LoginResult.error().message.c_str()));
                                }
                                SetReadyToDestroy();
                            });
              });
}

// ---- Restore ---------------------------------------------------------------

UDkRestoreAction* UDkRestoreAction::Restore(UObject* WorldContextObject)
{
    UDkRestoreAction* Action = NewObject<UDkRestoreAction>();
    Action->Subsystem = FindSubsystem(WorldContextObject);
    return Action;
}

void UDkRestoreAction::Activate()
{
    if (!Subsystem || !Subsystem->GetKit())
    {
        Completed.Broadcast(FDkSessionInfo(), TEXT("Dwarfkit is not configured"));
        return;
    }
    UDwarfkitSubsystem* LocalSubsystem = Subsystem;
    AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
              [this, LocalSubsystem]()
              {
                  auto RestoreResult = LocalSubsystem->GetKit()->restore();
                  AsyncTask(ENamedThreads::GameThread,
                            [this, LocalSubsystem,
                             RestoreResult = MoveTemp(RestoreResult)]() mutable
                            {
                                if (RestoreResult && *RestoreResult)
                                {
                                    LocalSubsystem->SetSession(*RestoreResult);
                                    Completed.Broadcast(InfoFor(*RestoreResult), FString());
                                }
                                else if (RestoreResult)
                                {
                                    Completed.Broadcast(FDkSessionInfo(),
                                                        TEXT("No stored session"));
                                }
                                else
                                {
                                    Completed.Broadcast(
                                        FDkSessionInfo(),
                                        UTF8_TO_TCHAR(
                                            RestoreResult.error().message.c_str()));
                                }
                                SetReadyToDestroy();
                            });
              });
}

// ---- Transact --------------------------------------------------------------

UDkTransactAction* UDkTransactAction::Transact(UObject* WorldContextObject,
                                               const FString& InActionJson, bool bInBroadcast)
{
    UDkTransactAction* Action = NewObject<UDkTransactAction>();
    Action->Subsystem = FindSubsystem(WorldContextObject);
    Action->ActionJson = InActionJson;
    Action->bBroadcast = bInBroadcast;
    return Action;
}

void UDkTransactAction::Activate()
{
    if (!Subsystem || !Subsystem->GetSession())
    {
        Completed.Broadcast(FString(), TEXT("No active session"));
        return;
    }
    std::shared_ptr<dwarfkit::Session> Session = Subsystem->GetSession();
    const std::string ActionText = TCHAR_TO_UTF8(*ActionJson);
    const bool bLocalBroadcast = bBroadcast;

    AsyncTask(
        ENamedThreads::AnyBackgroundThreadNormalTask,
        [this, Session, ActionText, bLocalBroadcast]()
        {
            const dwarfkit::json Parsed =
                dwarfkit::json::parse(ActionText, nullptr, false);
            FString TransactionId;
            FString Error;
            if (Parsed.is_discarded())
            {
                Error = TEXT("Invalid action JSON");
            }
            else
            {
                dwarfkit::TransactOptions Options;
                Options.broadcast = bLocalBroadcast;
                const auto TransactResult =
                    Session->transact({.action = Parsed}, Options);
                if (TransactResult)
                {
                    if (TransactResult->response &&
                        TransactResult->response->contains("transaction_id"))
                    {
                        TransactionId = UTF8_TO_TCHAR(
                            (*TransactResult->response)["transaction_id"]
                                .get<std::string>()
                                .c_str());
                    }
                }
                else
                {
                    Error = UTF8_TO_TCHAR(TransactResult.error().message.c_str());
                }
            }
            AsyncTask(ENamedThreads::GameThread,
                      [this, TransactionId, Error]()
                      {
                          Completed.Broadcast(TransactionId, Error);
                          SetReadyToDestroy();
                      });
        });
}
