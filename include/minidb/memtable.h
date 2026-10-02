#pragma once
#include "minidb/slice.h"
#include "minidb/status.h"
#include "minidb/iterator.h"
#include "minidb/arena.h"
#include "minidb/skiplist.h"
#include <string>

namespace minidb {

using SequenceNumber = uint64_t;

enum ValueType : char {
    kTypeDeletion = 0,
    kTypeValue = 1
};

struct MemEntry {
    ValueType type;
    Slice value;
};

class MemTable {
public:
    MemTable() = default;

    Iterator* NewIterator();    

    void Add(SequenceNumber seq, ValueType type,
             const Slice& key, const Slice& value);
    Status Get(const Slice& key, std::string* value);
    void Clear();
    size_t ApproximateMemoryUsage() const { return arena_.MemoryUsage(); }

    const SkipList<Slice, MemEntry>& table() const { return table_; }

private:
    Arena arena_;
    SkipList<Slice, MemEntry> table_{&arena_};
};

} // namespace minidb