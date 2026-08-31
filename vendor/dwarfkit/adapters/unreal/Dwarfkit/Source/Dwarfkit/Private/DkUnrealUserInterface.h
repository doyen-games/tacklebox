// dwarfkit::UserInterface bridged to UDwarfkitUI: calls arrive on the kit
// worker, are dispatched to the game thread and awaited with an FEvent.
#pragma once

#include "CoreMinimal.h"
#include "Async/Async.h"

THIRD_PARTY_INCLUDES_START
#include <dwarfkit/session.hpp>
THIRD_PARTY_INCLUDES_END

#include "DwarfkitUI.h"

class FDkUnrealUserInterface final : public dwarfkit::AbstractUserInterface
{
public:
    explicit FDkUnrealUserInterface(TWeakObjectPtr<UDwarfkitUI> InUI) : UI(InUI) {}

    dwarfkit::Result<dwarfkit::UserInterfaceLoginResponse> login(
        dwarfkit::LoginContext& Context) override
    {
        // chain and permission come from the async node arguments; the first
        // wallet plugin is used
        dwarfkit::UserInterfaceLoginResponse Response;
        if (Context.chain)
        {
            Response.chainId = Context.chain->id;
        }
        Response.permissionLevel = Context.permissionLevel;
        Response.walletPluginIndex = 0;
        return Response;
    }

    dwarfkit::Result<void> onError(const dwarfkit::Error& Error) override
    {
        RunOnGameThread([Message = FString(UTF8_TO_TCHAR(Error.message.c_str()))](
                            UDwarfkitUI* Widget) { Widget->OnErrorMessage(Message); });
        return {};
    }

    dwarfkit::Result<dwarfkit::UserInterfaceAccountCreationResponse> onAccountCreate(
        dwarfkit::CreateAccountContext&) override
    {
        return dwarfkit::err(dwarfkit::ErrorKind::Unsupported,
                             "Account creation UI is not implemented");
    }
    dwarfkit::Result<void> onAccountCreateComplete() override { return {}; }
    dwarfkit::Result<void> onLogin() override { return {}; }
    dwarfkit::Result<void> onLoginComplete() override { return {}; }
    dwarfkit::Result<void> onTransact() override { return {}; }
    dwarfkit::Result<void> onTransactComplete() override { return {}; }
    dwarfkit::Result<void> onSign() override { return {}; }
    dwarfkit::Result<void> onSignComplete() override { return {}; }
    dwarfkit::Result<void> onBroadcast() override { return {}; }
    dwarfkit::Result<void> onBroadcastComplete() override { return {}; }

    dwarfkit::Result<dwarfkit::PromptResponse> prompt(const dwarfkit::PromptArgs& Args,
                                                      dwarfkit::CancelToken) override
    {
        TArray<FDkPromptElement> Elements;
        for (const auto& Element : Args.elements)
        {
            FDkPromptElement Converted;
            switch (Element.type)
            {
                case dwarfkit::PromptElementType::qr: Converted.Type = TEXT("qr"); break;
                case dwarfkit::PromptElementType::link: Converted.Type = TEXT("link"); break;
                case dwarfkit::PromptElementType::button: Converted.Type = TEXT("button"); break;
                case dwarfkit::PromptElementType::countdown:
                    Converted.Type = TEXT("countdown");
                    break;
                case dwarfkit::PromptElementType::textarea:
                    Converted.Type = TEXT("textarea");
                    break;
                default: Converted.Type = TEXT("other"); break;
            }
            if (Element.label)
            {
                Converted.Label = UTF8_TO_TCHAR(Element.label->c_str());
            }
            Converted.DataJson = UTF8_TO_TCHAR(Element.data.dump().c_str());
            Elements.Add(MoveTemp(Converted));
        }
        RunOnGameThread(
            [Title = FString(UTF8_TO_TCHAR(Args.title.c_str())),
             Body = FString(UTF8_TO_TCHAR(Args.body.value_or("").c_str())),
             Elements = MoveTemp(Elements)](UDwarfkitUI* Widget)
            { Widget->OnPrompt(Title, Body, Elements); });
        return dwarfkit::PromptResponse{};
    }

    void status(const std::string& Message) override
    {
        RunOnGameThread([Text = FString(UTF8_TO_TCHAR(Message.c_str()))](UDwarfkitUI* Widget)
                        { Widget->OnStatus(Text); });
    }

private:
    // Dispatch to the game thread and block the worker until it ran, so UI
    // ordering matches the kit's flow.
    void RunOnGameThread(TFunction<void(UDwarfkitUI*)> Fn)
    {
        if (IsInGameThread())
        {
            if (UDwarfkitUI* Widget = UI.Get())
            {
                Fn(Widget);
            }
            return;
        }
        FEventRef Done{EEventMode::AutoReset};
        TWeakObjectPtr<UDwarfkitUI> LocalUI = UI;
        AsyncTask(ENamedThreads::GameThread,
                  [&Done, LocalUI, Fn = MoveTemp(Fn)]()
                  {
                      if (UDwarfkitUI* Widget = LocalUI.Get())
                      {
                          Fn(Widget);
                      }
                      Done->Trigger();
                  });
        Done->Wait();
    }

    TWeakObjectPtr<UDwarfkitUI> UI;
};
