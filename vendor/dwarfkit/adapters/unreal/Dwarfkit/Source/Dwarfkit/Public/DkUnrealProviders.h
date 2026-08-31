// Dwarfkit transport/storage providers over Unreal subsystems (BLUEPRINT.md
// 8.1). Kits must run on a worker thread; these providers block that worker,
// never the game thread.
#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include <dwarfkit/session.hpp>
#include <dwarfkit/transport/fetch_provider.hpp>
#include <dwarfkit/transport/websocket_provider.hpp>
THIRD_PARTY_INCLUDES_END

class IWebSocket;

// FHttpModule request issued from the worker; the worker blocks on an FEvent
// until the game-thread HTTP callback signals completion.
class DWARFKIT_API FDkUnrealFetchProvider final : public dwarfkit::FetchProvider
{
public:
    dwarfkit::Result<dwarfkit::FetchResponse> fetch(
        const dwarfkit::FetchRequest& Request) override;
};

// FWebSocketsModule socket; received frames queue behind a critical section
// and receive() blocks the worker on an FEvent with the requested timeout.
class DWARFKIT_API FDkUnrealWebSocketProvider final : public dwarfkit::WebSocketProvider
{
public:
    ~FDkUnrealWebSocketProvider() override;

    dwarfkit::Result<void> connect(std::string_view Url) override;
    dwarfkit::Result<dwarfkit::Bytes> receive(std::chrono::milliseconds Timeout,
                                              dwarfkit::CancelToken Token) override;
    dwarfkit::Result<void> send(std::span<const uint8_t> Data) override;
    void close() override;

private:
    TSharedPtr<IWebSocket> Socket;
    FCriticalSection QueueLock;
    TArray<TArray<uint8>> Frames;
    FEventRef FrameEvent{EEventMode::AutoReset};
    bool bClosed = false;
    bool bErrored = false;
};

// Session storage under FPaths::ProjectSavedDir()/Dwarfkit/.
class DWARFKIT_API FDkUnrealStorage final : public dwarfkit::SessionStorage
{
public:
    dwarfkit::Result<void> write(std::string_view Key, std::string_view Data) override;
    dwarfkit::Result<std::optional<std::string>> read(std::string_view Key) override;
    dwarfkit::Result<void> remove(std::string_view Key) override;

private:
    static FString PathFor(std::string_view Key);
};
