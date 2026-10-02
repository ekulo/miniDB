#include "minidb/status.h"
#include "minidb/slice.h"

namespace minidb {



Status Status::NotFound(const Slice& msg) {
    return Status(kNotFound, msg.ToString());
}

Status Status::Corruption(const Slice& msg) {
    return Status(kCorruption, msg.ToString());
}

Status Status::IOError(const Slice& msg) {
    return Status(kIOError, msg.ToString());
}

Status Status::Deleted(const Slice& msg) {
    return Status(kDeleted, msg.ToString());
}

std::string Status::ToString() const {
    if (ok()) return "OK";
    
    std::string result;
    switch (code_) {
        case kNotFound:    result = "NotFound: ";    break;
        case kCorruption:  result = "Corruption: ";  break;
        case kIOError:     result = "IOError: ";     break;
        case kDeleted:     result = "Deleted: "; break;
        default:           result = "Unknown: ";     break;
    }
    return result + msg_;
}


} // namespace minidb