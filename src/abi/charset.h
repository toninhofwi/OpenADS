#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openads::abi {

// Phase 1 placeholder: M1 treats input/output as already-correct byte
// sequences. Real OEM/ANSI/UTF translation lands alongside the `*W`
// entry-point variants in M9.17.
std::string to_internal(const std::uint8_t* p, std::size_t n);
void copy_to_caller(std::uint8_t* dst, std::uint16_t* dst_len_inout,
                    const std::string& src) noexcept;
// 32-bit length variant for AdsGetField-style entry points whose API
// length parameter is UNSIGNED32. Memo payloads can exceed 64 KB so
// the u16 variant truncates them to 65 534 bytes; this one writes up
// to (cap - 1) bytes plus a NUL when cap > 0, matching ACE semantics.
void copy_to_caller(std::uint8_t* dst, std::uint32_t* dst_len_inout,
                    const std::string& src) noexcept;

// UTF-16LE <-> UTF-8 codecs (M9.17). The engine stores byte
// sequences with no embedded codepage knowledge; the `*W` ABI
// variants transcode at the boundary so callers can pass / receive
// `WCHAR*` (UTF-16LE on Windows) directly.
//
// Decoder accepts unpaired surrogates and over-long inputs by
// substituting U+FFFD; both directions never throw.
std::string         utf16le_to_utf8(const std::uint16_t* in,
                                    std::size_t units);
std::vector<std::uint16_t> utf8_to_utf16le(const std::string& utf8);

// Safe UNSIGNED16 assignment — returns false when |n| exceeds 65535.
bool assign_u16(std::uint16_t* out, std::size_t n) noexcept;
std::uint16_t clamp_u16(std::size_t n) noexcept;

} // namespace openads::abi
