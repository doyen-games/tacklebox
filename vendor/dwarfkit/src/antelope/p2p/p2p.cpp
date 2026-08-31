#include <chrono>
#include <cstring>

#include <dwarfkit/antelope/p2p/client.hpp>

namespace dwarfkit::p2p {

// ---- BlockHeader -----------------------------------------------------------

Result<BlockId> BlockHeader::id() const {
    DK_TRY(encoded, Serializer::encode(*this));
    const auto digest = Checksum256::hash(encoded.array);
    return BlockId::fromBlockChecksum(digest, blockNum());
}

// ---- SimpleEnvelopeP2PProvider ---------------------------------------------

SimpleEnvelopeP2PProvider::SimpleEnvelopeP2PProvider(P2PProvider* nextProvider)
    : nextProvider_(nextProvider) {
    nextProvider_->onData(
        [this](std::span<const uint8_t> data) { handleData(data); });
    nextProvider_->onError([this](const Error& error) { emitError(error); });
}

void SimpleEnvelopeP2PProvider::handleData(std::span<const uint8_t> data) {
    remainingData_.insert(remainingData_.end(), data.begin(), data.end());
    while (remainingData_.size() >= 4) {
        uint32_t messageLength = 0;
        std::memcpy(&messageLength, remainingData_.data(), 4);
        if (messageLength > maxReadLength) {
            emitError(Error{ErrorKind::Invalid, "Incoming Message too long", 0, {}});
        }
        if (remainingData_.size() < 4 + static_cast<size_t>(messageLength)) {
            // need more data
            break;
        }
        const std::vector<uint8_t> message(remainingData_.begin() + 4,
                                           remainingData_.begin() + 4 + messageLength);
        remainingData_.erase(remainingData_.begin(),
                             remainingData_.begin() + 4 + messageLength);
        emitData(message);
    }
}

void SimpleEnvelopeP2PProvider::write(std::span<const uint8_t> data, P2PHandler done) {
    std::vector<uint8_t> framed(4 + data.size());
    const uint32_t length = static_cast<uint32_t>(data.size());
    std::memcpy(framed.data(), &length, 4);
    std::memcpy(framed.data() + 4, data.data(), data.size());
    nextProvider_->write(framed, std::move(done));
}

void SimpleEnvelopeP2PProvider::end(P2PHandler cb) {
    nextProvider_->end(std::move(cb));
}

void SimpleEnvelopeP2PProvider::destroy(const std::optional<Error>& error) {
    nextProvider_->destroy(error);
}

void SimpleEnvelopeP2PProvider::onClose(P2PHandler handler) {
    nextProvider_->onClose(std::move(handler));
}

void SimpleEnvelopeP2PProvider::emitData(std::span<const uint8_t> message) {
    for (const auto& handler : dataHandlers_) {
        handler(message);
    }
}

void SimpleEnvelopeP2PProvider::emitError(const Error& error) {
    for (const auto& handler : errorHandlers_) {
        handler(error);
    }
}

// ---- P2PClient -------------------------------------------------------------

P2PClient::P2PClient(const P2PClientOptions& options)
    : provider(options.provider),
      heartbeatTimoutMs_(options.heartbeatTimoutMs),
      setTimeoutImpl_(options.setTimeoutImpl) {
    if (heartbeatTimoutMs_) {
        resetHeartbeat();
    }
    if (provider) {
        provider->onData([this](std::span<const uint8_t> data) { handleData(data); });
        provider->onError([this](const Error& error) { emitError(error); });
        provider->onClose(
            [this]()
            {
                const auto listeners = closeListeners_;
                std::erase_if(closeListeners_, [](const auto& l) { return l.once; });
                for (const auto& listener : listeners) {
                    listener.handler();
                }
            });
    }
}

Result<void> P2PClient::send(const NetMessage& message, P2PHandler done) {
    if (!provider) {
        return err(ErrorKind::Invalid, "Missing provider");
    }
    DK_TRY(encoded, Serializer::encode(message));
    provider->write(encoded.array, std::move(done));
    return {};
}

void P2PClient::end(P2PHandler cb) {
    if (provider) {
        provider->end(std::move(cb));
    }
}

void P2PClient::destroy(const std::optional<Error>& error) {
    if (provider) {
        provider->destroy(error);
    }
}

size_t P2PClient::addMessage(P2PMessageHandler handler, bool once) {
    const size_t id = nextListenerId_++;
    messageListeners_.push_back({id, once, std::move(handler)});
    return id;
}

size_t P2PClient::onError(P2PErrorHandler handler) {
    const size_t id = nextListenerId_++;
    errorListeners_.push_back({id, false, std::move(handler)});
    return id;
}

size_t P2PClient::onClose(P2PHandler handler) {
    const size_t id = nextListenerId_++;
    closeListeners_.push_back({id, false, std::move(handler)});
    return id;
}

void P2PClient::removeListener(size_t id) {
    std::erase_if(messageListeners_, [id](const auto& l) { return l.id == id; });
    std::erase_if(errorListeners_, [id](const auto& l) { return l.id == id; });
    std::erase_if(closeListeners_, [id](const auto& l) { return l.id == id; });
}

void P2PClient::handleData(std::span<const uint8_t> data) {
    const auto message = Serializer::decode<NetMessage>(data);
    if (!message) {
        emitError(message.error());
        return;
    }
    const auto listeners = messageListeners_;
    std::erase_if(messageListeners_, [](const auto& l) { return l.once; });
    for (const auto& listener : listeners) {
        listener.handler(*message);
    }
}

void P2PClient::emitError(const Error& error) {
    const auto listeners = errorListeners_;
    std::erase_if(errorListeners_, [](const auto& l) { return l.once; });
    for (const auto& listener : listeners) {
        listener.handler(error);
    }
}

void P2PClient::resetHeartbeat() {
    if (heartbeatTimoutMs_ && setTimeoutImpl_) {
        setTimeoutImpl_([this]() { handleHeartbeat(); }, *heartbeatTimoutMs_);
    }
}

void P2PClient::handleHeartbeat() {
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    TimeMessage timeMessage;
    timeMessage.org = now;
    (void)send(timeMessage, [this]() { resetHeartbeat(); });
}

}  // namespace dwarfkit::p2p
