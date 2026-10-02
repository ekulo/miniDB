#include "minidb/wal.h"
#include "minidb/coding.h"

namespace minidb {

Status WALWriter::Open(const std::string& path) {
    // 追加模式打开，保证每次写入都在文件末尾
    file_.open(path, std::ios::binary | std::ios::app);
    if (!file_.is_open()) return Status::IOError("Failed to open WAL file for writing");
    return Status::OK();
}

Status WALWriter::AddRecord(const Slice& data) {
    // 1. 计算 CRC 校验和
    uint32_t crc = CRC32(data.data(), data.size());
    
    // 2. 准备 8 字节的头：[CRC (4字节)] [Length (4字节)]
    char header[8];
    EncodeFixed32(header, crc);
    EncodeFixed32(header + 4, data.size());

    // 3. 写入头部和数据
    file_.write(header, 8);
    file_.write(data.data(), data.size());

    if (!file_.good()) return Status::IOError("Failed to write to WAL");
    return Status::OK();
}

Status WALWriter::Sync() {
    file_.flush();
    return Status::OK();
}

Status WALWriter::Close() {
    if (file_.is_open()) file_.close();
    return Status::OK();
}

Status WALReader::Open(const std::string& path) {
    file_.open(path, std::ios::binary);
    if (!file_.is_open()) return Status::IOError("Failed to open WAL file for reading");
    return Status::OK();
}

Status WALReader::ReadRecord(Slice* record, std::string* scratch) {
    char header[8];
    
    // 1. 读头部
    file_.read(header, 8);
    if (file_.gcount() == 0) return Status::NotFound("End of WAL"); // 正常读完
    if (file_.gcount() < 8) return Status::Corruption("Truncated WAL header"); // 写到一半崩溃了

    uint32_t expected_crc = DecodeFixed32(header);
    uint32_t length = DecodeFixed32(header + 4);

    // 2. 读数据
    scratch->resize(length);
    file_.read(&(*scratch)[0], length);
    if (file_.gcount() < length) return Status::Corruption("Truncated WAL data");

    // 3. 校验 CRC
    uint32_t actual_crc = CRC32(scratch->data(), length);
    if (actual_crc != expected_crc) return Status::Corruption("WAL checksum mismatch");

    *record = Slice(*scratch);
    return Status::OK();
}

Status WALReader::Close() {
    if (file_.is_open()) file_.close();
    return Status::OK();
}

} // namespace minidb