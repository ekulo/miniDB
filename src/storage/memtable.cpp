#include "minidb/memtable.h"

namespace minidb {

void MemTable::Add(SequenceNumber seq, ValueType type,
                   const Slice& key, const Slice& value) {
    (void)seq; 
    // MVP 阶段先不用序列号
    // key 和 value 的数据都拷贝到 Arena 里，Slice 指向 Arena
    Slice key_copy = arena_.AllocateSlice(key);
    Slice value_copy = arena_.AllocateSlice(value);
    MemEntry entry{type, value_copy};
    table_.Insert(key_copy, entry);
}

Status  MemTable::Get(const Slice& key, std::string* value) {
    MemEntry entry;
    if (!table_.Find(key, &entry)) {
        return Status::NotFound("key not found");
    }
    if (entry.type == kTypeDeletion) {
        return Status::Deleted("key deleted");
    }
    *value = entry.value.ToString();
    return Status::OK();
}

void MemTable::Clear() {
    arena_.Reset();          // 1. 释放旧 Arena 的所有内存
    table_.Reset(&arena_);   // 2. 跳表在新的 Arena 上重建 head_
}

// 简单的 Map 迭代器实现


Iterator* MemTable::NewIterator() {
    return table_.NewIterator();
}


} // namespace minidb