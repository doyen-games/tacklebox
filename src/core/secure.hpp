// Memory hygiene for secrets. SecureBytes owns a buffer that is zeroized on
// destruction and best-effort locked out of swap. All key material and
// passwords in TackleBox travel through this type.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace tb {

// Zeroize that the optimizer cannot elide (trezor memzero underneath).
void secureWipe(void* p, size_t len);

// Constant-time equality (trezor consteq semantics). Sizes must match.
bool constTimeEq(std::span<const uint8_t> a, std::span<const uint8_t> b);

class SecureBytes {
public:
    SecureBytes() = default;
    explicit SecureBytes(size_t size);
    explicit SecureBytes(std::span<const uint8_t> data);
    explicit SecureBytes(std::string_view text);

    SecureBytes(const SecureBytes&) = delete;
    SecureBytes& operator=(const SecureBytes&) = delete;
    SecureBytes(SecureBytes&& other) noexcept;
    SecureBytes& operator=(SecureBytes&& other) noexcept;
    ~SecureBytes();

    uint8_t* data() { return data_; }
    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    std::span<uint8_t> span() { return {data_, size_}; }
    std::span<const uint8_t> span() const { return {data_, size_}; }
    std::string_view view() const {
        return {reinterpret_cast<const char*>(data_), size_};
    }

    // Wipe and release now.
    void clear();

private:
    void alloc(size_t size);

    uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

}  // namespace tb
