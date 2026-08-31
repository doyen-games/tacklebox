// OS CSPRNG shim. Implements dwarfkit::secureRandom (declared in
// antelope/utils.hpp, port of utils.ts secureRandom) and the C random_buffer
// symbol that trezor-crypto's rand.h declares, so no insecure fallback PRNG
// can ever link in.
#include <cstdlib>

#include <dwarfkit/antelope/utils.hpp>

#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#elif defined(__APPLE__)
#include <stdlib.h>
#else
#include <sys/random.h>
#endif

namespace dwarfkit {

static bool fillRandom(uint8_t* buf, size_t len) {
#if defined(_WIN32)
    return BCRYPT_SUCCESS(
        BCryptGenRandom(nullptr, buf, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
#elif defined(__APPLE__)
    arc4random_buf(buf, len);
    return true;
#else
    size_t filled = 0;
    while (filled < len) {
        const ssize_t got = getrandom(buf + filled, len - filled, 0);
        if (got < 0) return false;
        filled += static_cast<size_t>(got);
    }
    return true;
#endif
}

Result<std::vector<uint8_t>> secureRandom(size_t length) {
    std::vector<uint8_t> result(length);
    if (length > 0 && !fillRandom(result.data(), length)) {
        return err(ErrorKind::Internal, "No secure random source available");
    }
    return result;
}

}  // namespace dwarfkit

extern "C" void random_buffer(uint8_t* buf, size_t len) {
    if (!dwarfkit::fillRandom(buf, len)) {
        // trezor-crypto has no error path for this; failing open is not an option
        std::abort();
    }
}
