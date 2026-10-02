#pragma once

#include <string>

namespace minidb {

// 前向声明，避免循环包含，告诉编译器有个叫 Slice 的类
class Slice;

class Status {
public:
    Status() : code_(kOk) {}
    
    static Status OK() { return Status(); }
    static Status NotFound(const Slice& msg);
    static Status Corruption(const Slice& msg);
    static Status IOError(const Slice& msg);

    bool ok() const { return code_ == kOk; }
    bool IsNotFound() const { return code_ == kNotFound; } 
    std::string ToString() const;
    bool IsDeleted() const { return code_ == kDeleted; } 
    static Status Deleted(const Slice& msg);

private:
    enum Code { kOk = 0, kNotFound = 1, kCorruption = 2, kIOError = 3,kDeleted = 4 };
    
    explicit Status(Code code, const std::string& msg)
        : code_(code), msg_(msg) {}

    Code code_;
    std::string msg_;
};

} // namespace minidb