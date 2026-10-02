#pragma once
#include "minidb/slice.h"
#include "minidb/status.h"
#include "minidb/memtable.h"
#include "minidb/iterator.h"
#include <fstream>
#include <map>
#include <string>

namespace minidb {

struct SSTableEntry {
    ValueType type;
    std::string value;
};

class SSTableBuilder {
public:
    SSTableBuilder() = default;
    ~SSTableBuilder();

    Status Open(const std::string& path);
    Status Add(const Slice& key, ValueType type, const Slice& value);
    Status Finish();
    Status Close();

private:
    std::ofstream file_;
    std::string path_;
    uint64_t count_ = 0;
};

class SSTableReader {
public:
    SSTableReader() = default;
    
    Status Open(const std::string& path);
    Status Get(const Slice& key, std::string* value);
    Iterator* NewIterator();   // 🌟 新增
    Status Close();
    const std::map<std::string, SSTableEntry>& table() const { return table_; }

private:
    std::map<std::string, SSTableEntry> table_;
};

} // namespace minidb