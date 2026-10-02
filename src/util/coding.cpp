#include "minidb/coding.h"

namespace minidb {

// 小端字节序（Little Endian）编码，保证跨平台磁盘文件格式一致
void EncodeFixed32(char* buf, uint32_t value) {
    buf[0] = static_cast<char>(value & 0xff);
    buf[1] = static_cast<char>((value >> 8) & 0xff);
    buf[2] = static_cast<char>((value >> 16) & 0xff);
    buf[3] = static_cast<char>((value >> 24) & 0xff);
}

void EncodeFixed64(char* buf, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        buf[i] = static_cast<char>((value >> (i * 8)) & 0xff);
    }
}

uint32_t DecodeFixed32(const char* buf) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(buf[0]))) |
           (static_cast<uint32_t>(static_cast<unsigned char>(buf[1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(buf[2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(buf[3])) << 24);
}

uint64_t DecodeFixed64(const char* buf) {
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        result |= (static_cast<uint64_t>(static_cast<unsigned char>(buf[i])) << (i * 8));
    }
    return result;
}

// CRC32 查表法实现，用于校验数据完整性
static uint32_t kCRC32Table[256];
static bool kCRC32TableInit = false;

static void InitCRC32Table() {
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        kCRC32Table[i] = crc;
    }
    kCRC32TableInit = true;
}

uint32_t CRC32(const char* data, size_t n) {
    if (!kCRC32TableInit) {
        InitCRC32Table();
    }
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc = (crc >> 8) ^ kCRC32Table[(crc ^ static_cast<unsigned char>(data[i])) & 0xff];
    }
    return crc ^ 0xFFFFFFFF;
}

} // namespace minidb