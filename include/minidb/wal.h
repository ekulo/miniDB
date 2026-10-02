#pragma once
#include "minidb/status.h"
#include "minidb/slice.h"
#include <fstream>
#include <string>

namespace minidb {

class WALWriter {
public:
    WALWriter() = default;
    ~WALWriter() = default;

    Status Open(const std::string& path);
    Status AddRecord(const Slice& data);
    Status Sync();
    Status Close();

private:
    std::ofstream file_;
};

class WALReader {
public:
    WALReader() = default;
    ~WALReader() = default;

    Status Open(const std::string& path);
    // 读取一条记录，返回 OK / NotFound(读完了) / Corruption(数据损坏)
    Status ReadRecord(Slice* record, std::string* scratch);
    Status Close();

private:
    std::ifstream file_;
};

} // namespace minidb