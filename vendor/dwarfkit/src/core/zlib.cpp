#include <dwarfkit/core/zlib.hpp>

#include <zlib.h>

namespace dwarfkit {

namespace {

Result<std::vector<uint8_t>> deflateImpl(std::span<const uint8_t> data, int windowBits) {
    z_stream stream{};
    // pako defaults: level 6, memLevel 8, default strategy
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        return err(ErrorKind::Internal, "deflate init failed");
    }
    std::vector<uint8_t> out(deflateBound(&stream, static_cast<uLong>(data.size())));
    stream.next_in = data.data();
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    const int status = deflate(&stream, Z_FINISH);
    const auto total = stream.total_out;
    deflateEnd(&stream);
    if (status != Z_STREAM_END) {
        return err(ErrorKind::Internal, "deflate failed");
    }
    out.resize(total);
    return out;
}

Result<std::vector<uint8_t>> inflateImpl(std::span<const uint8_t> data, int windowBits) {
    z_stream stream{};
    if (inflateInit2(&stream, windowBits) != Z_OK) {
        return err(ErrorKind::Internal, "inflate init failed");
    }
    std::vector<uint8_t> out(data.size() * 4 + 64);
    stream.next_in = data.data();
    stream.avail_in = static_cast<uInt>(data.size());
    size_t written = 0;
    for (;;) {
        stream.next_out = out.data() + written;
        stream.avail_out = static_cast<uInt>(out.size() - written);
        const int status = inflate(&stream, Z_NO_FLUSH);
        written = stream.total_out;
        if (status == Z_STREAM_END) {
            break;
        }
        if (status == Z_OK || status == Z_BUF_ERROR) {
            if (stream.avail_out == 0) {
                out.resize(out.size() * 2);
                continue;
            }
            inflateEnd(&stream);
            return err(ErrorKind::Invalid, "invalid compressed data");
        }
        inflateEnd(&stream);
        return err(ErrorKind::Invalid, "invalid compressed data");
    }
    inflateEnd(&stream);
    out.resize(written);
    return out;
}

}  // namespace

Result<std::vector<uint8_t>> zlibCompress(std::span<const uint8_t> data) {
    return deflateImpl(data, 15);
}

Result<std::vector<uint8_t>> zlibUncompress(std::span<const uint8_t> data) {
    return inflateImpl(data, 15);
}

Result<std::vector<uint8_t>> deflateRaw(std::span<const uint8_t> data) {
    return deflateImpl(data, -15);
}

Result<std::vector<uint8_t>> inflateRaw(std::span<const uint8_t> data) {
    return inflateImpl(data, -15);
}

}  // namespace dwarfkit
