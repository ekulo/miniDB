#pragma once
#include <cstring>
#include <string>

namespace minidb {

class Slice {
public:
    Slice() : data_(""), size_(0) {}

    Slice(const char* s) : data_(s), size_(s ? std::strlen(s) : 0) {}
    Slice(const char* d, size_t n) : data_(d), size_(n) {}
    Slice(const std::string& s) : data_(s.data()), size_(s.size()) {}

    const char* data() const { return data_; }
    size_t size() const { return size_; }
    std::string ToString() const { return std::string(data_, size_); }

private:
    const char* data_;
    size_t size_;
};

} // namespace minidb