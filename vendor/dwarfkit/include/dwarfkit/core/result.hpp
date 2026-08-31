#pragma once

#include <string>
#include <utility>

#include <tl/expected.hpp>

#include <dwarfkit/core/json.hpp>

namespace dwarfkit {

enum class ErrorKind {
    Invalid,
    Api,
    Transport,
    Canceled,
    Plugin,
    Storage,
    Unsupported,
    NotFound,
    Internal,
};

struct Error {
    ErrorKind kind = ErrorKind::Internal;
    std::string message;
    int code = 0;   // ErrorKind::Api: HTTP status
    json details;   // ErrorKind::Api: chain error object
};

template <class T>
using Result = tl::expected<T, Error>;

// One-liner error return: return err(ErrorKind::Invalid, "Odd number of hex digits");
inline tl::unexpected<Error> err(ErrorKind kind, std::string message, int code = 0,
                                 json details = nullptr) {
    return tl::unexpected(Error{kind, std::move(message), code, std::move(details)});
}

inline tl::unexpected<Error> err(Error error) { return tl::unexpected(std::move(error)); }

}  // namespace dwarfkit

#define DK_CAT_IMPL(a, b) a##b
#define DK_CAT(a, b) DK_CAT_IMPL(a, b)

// Bind the success value of a Result expression or propagate its error.
#define DK_TRY(var, expr)                                                     \
    auto&& DK_CAT(_dk_r, __LINE__) = (expr);                                  \
    if (!DK_CAT(_dk_r, __LINE__))                                             \
        return ::tl::unexpected(std::move(DK_CAT(_dk_r, __LINE__).error()));  \
    auto var = std::move(*DK_CAT(_dk_r, __LINE__));

// Propagate the error of a Result expression, discarding the value.
#define DK_CHECK(expr)                                                        \
    {                                                                         \
        auto&& DK_CAT(_dk_r, __LINE__) = (expr);                              \
        if (!DK_CAT(_dk_r, __LINE__))                                         \
            return ::tl::unexpected(                                          \
                std::move(DK_CAT(_dk_r, __LINE__).error()));                  \
    }
