// OS CSPRNG. Aborts the process if the OS entropy source fails: a wallet must
// never continue with weak randomness.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tb {

void randomBytes(uint8_t* out, size_t len);

}  // namespace tb
