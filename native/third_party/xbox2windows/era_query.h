#pragma once

#include <cstddef>

// Desktop handler for XMemVirtualQuery, shaped by the four observed BVH call sites.
// Bytes 0x00-0x2F use the Win32 MEMORY_BASIC_INFORMATION layout: the callers read State at
// +0x20 and Protect at +0x24, which match it. The byte at +0x30 has no public definition.
// Here it carries a desktop policy flag: bit 0 is set when the queried address lies in a
// region registered with register_gpu_region. It is not a confirmed Scarlett field.
namespace era_query {
constexpr std::size_t tail_offset = 0x30;
constexpr unsigned char tail_registered = 0x01;

// Regions must not overlap. Returns false for null, empty, wrapping or overlapping input.
bool register_gpu_region(const void* base, std::size_t size) noexcept;
bool unregister_gpu_region(const void* base) noexcept;

// Writes MEMORY_BASIC_INFORMATION to buffer[0, 0x30). When length > 0x30 it also writes the
// tail byte at +0x30 (registered: 0x01, otherwise 0x00) and leaves later bytes untouched.
// Returns the number of MEMORY_BASIC_INFORMATION bytes written, or 0 on failure.
std::size_t query(void* process, const void* address, void* buffer, std::size_t length) noexcept;
}
