#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cassert>
#include "minidb/slice.h"

namespace minidb {

class Arena {
public:
    Arena() = default;
    ~Arena() {
        for (char* block : blocks_) delete[] block;
    }

    // 禁止拷贝
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    // 分配内存（未初始化）
    char* Allocate(size_t bytes) {
        if (bytes == 0) return nullptr;
        if (bytes > kBlockSize / 4) {
            // 大块单独分配
            char* result = new char[bytes];
            blocks_.push_back(result);
            return result;
        }
        if (remaining_ < bytes) {
            // 当前块不够，申请新块
            char* new_block = new char[kBlockSize];
            blocks_.push_back(new_block);
            current_ = new_block;
            remaining_ = kBlockSize;
        }
        char* result = current_;
        current_ += bytes;
        remaining_ -= bytes;
        return result;
    }

    Slice AllocateSlice(const Slice& s) {
        char* mem = Allocate(s.size());
        std::memcpy(mem, s.data(), s.size());
        return Slice(mem, s.size());
    }


    size_t MemoryUsage() const {
        return blocks_.size() * kBlockSize;
    }

    // 清空所有已分配的内存，重新开始（保留 Arena 对象本身）
    void Reset() {
        for (char* block : blocks_) delete[] block;
        blocks_.clear();
        current_ = nullptr;
        remaining_ = 0;
    }

private:
    static constexpr size_t kBlockSize = 4096;
    std::vector<char*> blocks_;
    char* current_ = nullptr;
    size_t remaining_ = 0;
};

} // namespace minidb