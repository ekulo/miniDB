#pragma once
#include <cstdint>
#include <cstddef>

namespace minidb {

void EncodeFixed32(char* buf, uint32_t value);
void EncodeFixed64(char* buf, uint64_t value);
uint32_t DecodeFixed32(const char* buf);
uint64_t DecodeFixed64(const char* buf);

uint32_t CRC32(const char* data, size_t n);

} // namespace minidb