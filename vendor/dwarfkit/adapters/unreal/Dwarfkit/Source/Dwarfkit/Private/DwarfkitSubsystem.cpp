#include "DwarfkitSubsystem.h"

#include "DkUnrealProviders.h"
#include "DkUnrealUserInterface.h"

void UDwarfkitSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UDwarfkitSubsystem::Deinitialize()
{
    Session.reset();
    Kit.Reset();
    Super::Deinitialize();
}

void UDwarfkitSubsystem::Configure(const FString& AppName, const FString& ChainId,
                                   const FString& Url)
{
    const auto ParsedId =
        dwarfkit::Checksum256::from(std::string(TCHAR_TO_UTF8(*ChainId)));
    if (!ParsedId)
    {
        UE_LOG(LogTemp, Error, TEXT("Dwarfkit: invalid chain id %s"), *ChainId);
        return;
    }

    dwarfkit::SessionKitArgs Args;
    Args.appName = TCHAR_TO_UTF8(*AppName);
    Args.chains = {dwarfkit::ChainDefinition::from(
        {.id = *ParsedId, .url = std::string(TCHAR_TO_UTF8(*Url))})};
    Args.ui = std::make_shared<FDkUnrealUserInterface>(UI);

    dwarfkit::WalletPluginAnchorOptions AnchorOptions;
    AnchorOptions.buoyWs = std::make_shared<FDkUnrealWebSocketProvider>();
    Args.walletPlugins = {std::make_shared<dwarfkit::WalletPluginAnchor>(AnchorOptions)};

    dwarfkit::SessionKitOptions Options;
    Options.fetch = std::make_shared<FDkUnrealFetchProvider>();
    Options.storage = std::make_shared<dwarfkit::FileSessionStorage>(
        std::string(TCHAR_TO_UTF8(*FPaths::Combine(FPaths::ProjectSavedDir(),
                                                   TEXT("Dwarfkit")))));

    Kit = MakeUnique<dwarfkit::SessionKit>(Args, Options);
}

void UDwarfkitSubsystem::SetUI(UDwarfkitUI* InUI)
{
    UI = InUI;
}

bool UDwarfkitSubsystem::HasSession() const
{
    return Session != nullptr;
}

FDkSessionInfo UDwarfkitSubsystem::GetSessionInfo() const
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
