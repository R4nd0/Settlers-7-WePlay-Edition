/* Canonical release compressor. zlib-ng is linked in native API mode so its
 * symbols cannot interpose on Python's independently loaded system zlib. */
#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "zlib-ng.h"

const char *weplay_zlib_version(void) { return zlibng_version(); }

size_t weplay_gzip_bound(size_t length) {
    if (length > UINT_MAX) return 0;
    return (size_t)zng_compressBound(length) + 32;
}

int weplay_gzip(const uint8_t *input, size_t length, uint8_t *output,
                size_t capacity, size_t *written) {
    if (!input || !output || !written || length > UINT_MAX || capacity > UINT_MAX || capacity < 32) return -1;
    zng_stream stream;
    memset(&stream, 0, sizeof(stream));
    int code = zng_deflateInit2(&stream, 9, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY);
    if (code != Z_OK) return code;
    stream.next_in = (uint8_t *)input;
    stream.avail_in = (uint32_t)length;
    stream.next_out = output;
    stream.avail_out = (uint32_t)capacity;
    code = zng_deflate(&stream, Z_FINISH);
    *written = (size_t)stream.total_out;
    int complete = code == Z_STREAM_END && stream.avail_in == 0 && *written >= 18;
    int ended = zng_deflateEnd(&stream);
    if (!complete) return code == Z_OK ? Z_BUF_ERROR : code;
    if (ended != Z_OK) return ended;
    output[9] = 255;
    return Z_OK;
}
