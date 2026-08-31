#include "core/secure.hpp"

#include <cstring>

extern "C" {
#include <memzero.h>
}

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace tb {

void secureWipe(void* p, size_t len) {
    if (p && len) memzero(p, len);
}

bool constTimeEq(std::span<const uint8_t> a, std::span<const uint8_t> b) {
    if (a.size() != b.size()) return false;
    uint8_t acc = 0;
    for (size_t i = 0; i < a.size(); ++i) acc = static_cast<uint8_t>(acc | (a[i] ^ b[i]));
    return acc == 0;
}

SecureBytes::SecureBytes(size_t size) { alloc(size); }

SecureBytes::SecureBytes(std::span<const uint8_t> data) {
    alloc(data.size());
    if (size_) std::memcpy(data_, data.data(), size_);
}

SecureBytes::SecureBytes(std::string_view text) {
    alloc(text.size());
    if (size_) std::memcpy(data_, text.data(), size_);
}

SecureBytes::SecureBytes(SecureBytes&& other) noexcept
    : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
}

SecureBytes& SecureBytes::operator=(SecureBytes&& other) noexcept {
    if (this != &other) {
        clear();
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

SecureBytes::~SecureBytes() { clear(); }

void SecureBytes::alloc(size_t size) {
    size_ = size;
    if (!size) return;
    data_ = new uint8_t[size]();
#ifdef _WIN32
    VirtualLock(data_, size);  // best effort: keep out of the pagefile
#else
    mlock(data_, size);  // best effort: EPERM/ENOMEM are fine, we still run
#endif
}

void SecureBytes::clear() {
    if (data_) {
        secureWipe(data_, size_);
#ifdef _WIN32
        VirtualUnlock(data_, size_);
#else
        munlock(data_, size_);
#endif
        delete[] data_;
    }
    data_ = nullptr;
    size_ = 0;
}

}  // namespace tb
