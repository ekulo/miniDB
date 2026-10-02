#include "minidb/wal.h"
#include "minidb/status.h"
#include <cassert>
#include <iostream>
#include <cstdio>

int main() {
    std::string path = "test_wal.log";
    std::remove(path.c_str()); // 清理上次运行残留的文件

    // 1. 测试写入
    minidb::WALWriter writer;
    assert(writer.Open(path).ok());
    assert(writer.AddRecord(minidb::Slice("key1")).ok());
    assert(writer.AddRecord(minidb::Slice("key2")).ok());
    writer.Sync();
    writer.Close();

    // 2. 测试读取和校验
    minidb::WALReader reader;
    assert(reader.Open(path).ok());
    minidb::Slice record;
    std::string scratch;
    
    auto s1 = reader.ReadRecord(&record, &scratch);
    assert(s1.ok());
    assert(record.ToString() == "key1");

    auto s2 = reader.ReadRecord(&record, &scratch);
    assert(s2.ok());
    assert(record.ToString() == "key2");

    auto s3 = reader.ReadRecord(&record, &scratch);
    assert(s3.IsNotFound()); // 读到最后应该是 NotFound

    reader.Close();
    std::remove(path.c_str()); // 清理测试文件

    std::cout << "test_wal passed" << std::endl;
    return 0;
}