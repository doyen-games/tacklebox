#include "DkUnrealProviders.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "IWebSocket.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "WebSocketsModule.h"

namespace
{

FString ToFString(std::string_view Value)
{
    const std::string Utf8(Value);
    return FString(UTF8_TO_TCHAR(Utf8.c_str()));
}

std::string ToStdString(const FString& Value)
{
    return std::string(TCHAR_TO_UTF8(*Value));
}

}  // namespace

// ---- FDkUnrealFetchProvider ------------------------------------------------

dwarfkit::Result<dwarfkit::FetchResponse> FDkUnrealFetchProvider::fetch(
    const dwarfkit::FetchRequest& Request)
{
    // The worker blocks on this event; the HTTP callback fires on the game
    // thread and signals it.
    FEventRef Done{EEventMode::AutoReset};
    dwarfkit::FetchResponse Response;
    bool bSucceeded = false;

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Http =
        FHttpModule::Get().CreateRequest();
    Http->SetURL(ToFString(Request.url));
    Http->SetVerb(ToFString(Request.method));
    if (!Request.body.empty())
    {
        Http->SetContentAsString(ToFString(Request.body));
    }
    for (const auto& Header : Request.headers)
    {
        Http->SetHeader(ToFString(Header.first), ToFString(Header.second));
    }

    Http->OnProcessRequestComplete().BindLambda(
        [&Done, &Response, &bSucceeded](FHttpRequestPtr, FHttpResponsePtr HttpResponse,
                                        bool bConnected)
        {
            if (bConnected && HttpResponse.IsValid())
            {
                Response.status = HttpResponse->GetResponseCode();
                Response.body = ToStdString(HttpResponse->GetContentAsString());
                for (const FString& Header : HttpResponse->GetAllHeaders())
                {
                    FString Key;
                    FString Value;
                    if (Header.Split(TEXT(": "), &Key, &Value))
                    {
                        Response.headers.emplace_back(ToStdString(Key), ToStdString(Value));
                    }
                }
                bSucceeded = true;
            }
            Done->Trigger();
        });
    Http->ProcessRequest();
    Done->Wait();

    if (!bSucceeded)
    {
        return dwarfkit::err(dwarfkit::ErrorKind::Transport, "HTTP request failed");
    }
    return Response;
}

// ---- FDkUnrealWebSocketProvider --------------------------------------------

FDkUnrealWebSocketProvider::~FDkUnrealWebSocketProvider()
{
    close();
}

dwarfkit::Result<void> FDkUnrealWebSocketProvider::connect(std::string_view Url)
{
    close();
    bClosed = false;
    bErrored = false;

    FEventRef Connected{EEventMode::AutoReset};
    bool bOk = false;

    Socket = FWebSocketsModule::Get().CreateWebSocket(ToFString(Url));
    Socket->OnConnected().AddLambda(
        [&Connected, &bOk]()
        {
            bOk = true;
            Connected->Trigger();
        });
    Socket->OnConnectionError().AddLambda(
        [&Connected](const FString&) { Connected->Trigger(); });
    Socket->OnClosed().AddLambda(
        [this](int32, const FString&, bool)
        {
            FScopeLock Lock(&QueueLock);
            bErrored = true;
            FrameEvent->Trigger();
        });
    Socket->OnMessage().AddLambda(
        [this](const FString& Message)
        {
            const std::string Utf8 = ToStdString(Message);
            FScopeLock Lock(&QueueLock);
            Frames.Emplace(TArray<uint8>(reinterpret_cast<const uint8*>(Utf8.data()),
                                         static_cast<int32>(Utf8.size())));
            FrameEvent->Trigger();
        });
    Socket->OnRawMessage().AddLambda(
        [this](const void* Data, SIZE_T Size, SIZE_T BytesRemaining)
        {
            if (BytesRemaining > 0)
            {
                return;  // partial frames buffer engine-side
            }
            FScopeLock Lock(&QueueLock);
            Frames.Emplace(
                TArray<uint8>(static_cast<const uint8*>(Data), static_cast<int32>(Size)));
            FrameEvent->Trigger();
        });
    Socket->Connect();
    Connected->Wait();

    if (!bOk)
    {
        Socket.Reset();
        dwarfkit::Error Error;
        Error.kind = dwarfkit::ErrorKind::Transport;
        Error.message = "WebSocket connect failed";
        Error.details = dwarfkit::json{{"code", "E_NETWORK"}};
        return dwarfkit::err(MoveTemp(Error));
    }
    return {};
}

dwarfkit::Result<dwarfkit::Bytes> FDkUnrealWebSocketProvider::receive(
    std::chrono::milliseconds Timeout, dwarfkit::CancelToken Token)
{
    const double Deadline =
        FPlatformTime::Seconds() + static_cast<double>(Timeout.count()) / 1000.0;
    for (;;)
    {
        {
            FScopeLock Lock(&QueueLock);
            if (Frames.Num() > 0)
            {
                TArray<uint8> Frame = MoveTemp(Frames[0]);
                Frames.RemoveAt(0);
                return dwarfkit::Bytes(
                    std::vector<uint8_t>(Frame.GetData(), Frame.GetData() + Frame.Num()));
            }
            if (bErrored || bClosed)
            {
                dwarfkit::Error Error;
                Error.kind = dwarfkit::ErrorKind::Transport;
                Error.message = "WebSocket closed";
                Error.details = dwarfkit::json{{"code", "E_NETWORK"}};
                return dwarfkit::err(MoveTemp(Error));
            }
        }
        if (Token.cancelled())
        {
            return dwarfkit::err(dwarfkit::ErrorKind::Canceled, "Cancelled");
        }
        const double Remaining = Deadline - FPlatformTime::Seconds();
        if (Remaining <= 0)
        {
            dwarfkit::Error Error;
            Error.kind = dwarfkit::ErrorKind::Transport;
            Error.message = "WebSocket receive timed out";
            Error.details = dwarfkit::json{{"code", "E_TIMEOUT"}};
            return dwarfkit::err(MoveTemp(Error));
        }
        // wake regularly to observe cancellation
        const uint32 SliceMs =
            static_cast<uint32>(FMath::Clamp(Remaining * 1000.0, 1.0, 250.0));
        FrameEvent->Wait(SliceMs);
    }
}

dwarfkit::Result<void> FDkUnrealWebSocketProvider::send(std::span<const uint8_t> Data)
{
    if (!Socket.IsValid() || !Socket->IsConnected())
    {
        return dwarfkit::err(dwarfkit::ErrorKind::Transport, "WebSocket is not connected");
    }
    Socket->Send(Data.data(), Data.size(), /*bIsBinary*/ true);
    return {};
}

void FDkUnrealWebSocketProvider::close()
{
    if (Socket.IsValid())
    {
        Socket->Close();
        Socket.Reset();
    }
    FScopeLock Lock(&QueueLock);
    bClosed = true;
    FrameEvent->Trigger();
}

// ---- FDkUnrealStorage ------------------------------------------------------

FString FDkUnrealStorage::PathFor(std::string_view Key)
{
    return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Dwarfkit"),
                           ToFString(Key) + TEXT(".txt"));
}

dwarfkit::Result<void> FDkUnrealStorage::write(std::string_view Key, std::string_view Data)
{
    if (!FFileHelper::SaveStringToFile(ToFString(Data), *PathFor(Key)))
    {
        return dwarfkit::err(dwarfkit::ErrorKind::Storage, "Failed to write session storage");
    }
    return {};
}

dwarfkit::Result<std::optional<std::string>> FDkUnrealStorage::read(std::string_view Key)
{
    FString Data;
    if (!FPaths::FileExists(PathFor(Key)))
    {
        return std::optional<std::string>();
    }
    if (!FFileHelper::LoadFileToString(Data, *PathFor(Key)))
    {
        return dwarfkit::err(dwarfkit::ErrorKind::Storage, "Failed to read session storage");
    }
    return std::optional(ToStdString(Data));
}

dwarfkit::Result<void> FDkUnrealStorage::remove(std::string_view Key)
{
    IFileManager::Get().Delete(*PathFor(Key), /*RequireExists*/ false);
    return {};
}
