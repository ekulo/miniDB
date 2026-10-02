#pragma once
#include "minidb/status.h"
#include "minidb/slice.h"
#include "minidb/iterator.h"
#include <string>

namespace minidb {

struct Options {
    bool create_if_missing = false;
    size_t memtable_size_limit = 4 * 1024 * 1024; // 4MB
    int compaction_trigger = 4;   // 🌟 文件数 ≥ 4 就触发 Compaction
};

class DB {
public:
    static Status Open(const Options& options, const std::string& name, DB** dbptr);

    virtual Status Put(const Slice& key, const Slice& value) = 0;
    virtual Status Get(const Slice& key, std::string* value) = 0;
    virtual Status Delete(const Slice& key) = 0;
    virtual Iterator* NewIterator() = 0;

    virtual ~DB() = default;
};

} // namespace minidb