#include "minidb/status.h"
#include "minidb/coding.h"
#include <cassert>
#include <iostream>

int main() {
    auto s = minidb::Status::OK();
    assert(s.ok());

    char buf[4];
    minidb::EncodeFixed32(buf, 12345);
    assert(minidb::DecodeFixed32(buf) == 12345);

    std::cout << "test_status passed" << std::endl;
    return 0;
}