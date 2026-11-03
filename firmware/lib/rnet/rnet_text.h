// Text helpers for diagnostics: hex dump/parse and a one-line human description of a frame.
// Used by the serial console (M2) and later by frame tracing. Pure C++17, no heap.
#pragma once

#include <cstddef>
#include <cstdint>

#include "rnet_codec.h"

namespace rnet {

// Writes "F0 00 7F ..." (uppercase, space separated) into `out`, always NUL-terminated.
// Truncates with "..." if `cap` is too small. Returns the number of characters written.
size_t format_hex(const uint8_t* data, size_t len, char* out, size_t cap);

// Parses hex bytes separated by spaces, commas or nothing ("F0 00", "F0,00", "f000", "0xF0 0x00").
// Returns false on a bad digit, an odd digit count in a token, or more than `cap` bytes.
bool parse_hex(const char* text, uint8_t* out, size_t cap, size_t& len);

// One-line description of a decoded frame, e.g.
//   "SetData 00.02.70<-00.00.7F ZoneInfo z3 on src=2 vol=24 bass=0 treble=-2 loud=off bal=0 sys=on"
// Always NUL-terminated; truncated if `cap` is too small.
void describe(const Frame& frame, char* out, size_t cap);

}  // namespace rnet
