#pragma once
#include "minidb/slice.h"
#include "minidb/status.h"


namespace minidb {

class Iterator {
public:
    virtual ~Iterator() = default;

    // 游标是否指向有效数据
    virtual bool Valid() const = 0;
    
    // 移动到第一个/最后一个/指定的 key
    virtual void SeekToFirst() = 0;
    virtual void Seek(const Slice& target) = 0;
    
    // 移动到下一个
    virtual void Next() = 0;

    // 获取当前 key 和 value
    virtual Slice key() const = 0;
    virtual Slice value() const = 0;

    // 返回当前迭代器的状态（是否有错误）
    virtual Status status() const = 0;

};

} // namespace minidb