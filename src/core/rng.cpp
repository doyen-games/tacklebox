#include "core/rng.hpp"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>

#include <bcrypt.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__) || defined(__OpenBSD__)
#include <stdlib.h>  // arc4random_buf
#endif
#endif

namespace tb {

void randomBytes(uint8_t* out, size_t len) {
#ifdef _WIN32
    NTSTATUS st = BCryptGenRandom(nullptr, out, static_cast<ULONG>(len),
                                  BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (st != 0) {
        std::fprintf(stderr, "fatal: BCryptGenRandom failed (0x%08lx)\n",
                     static_cast<unsigned long>(st));
        std::abort();
    }
#elif defined(__APPLE__) || defined(__OpenBSD__)
    arc4random_buf(out, len);
#else
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        std::fprintf(stderr, "fatal: cannot open /dev/urandom\n");
        std::abort();
    }
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, out + got, len - got);
        if (n <= 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "fatal: /dev/urandom read failed\n");
            std::abort();
        }
        got += static_cast<size_t>(n);
    }
    close(fd);
#endif
}

}  // namespace tb
