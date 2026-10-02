#include "minidb/db.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <vector>
#include "minidb/iterator.h"

int main() {
    std::string dbname = "./testdb";
    std::filesystem::remove_all(dbname);

    minidb::Options options;
    options.create_if_missing = true;

    // 第一次打开，写数据
    {
        minidb::DB* db;
        assert(minidb::DB::Open(options, dbname, &db).ok());
        assert(db->Put("name", "Alice").ok());
        assert(db->Put("age", "20").ok());
        for (int i = 0; i < 10000; ++i) {
             db->Put("key_" + std::to_string(i), "value_" + std::to_string(i));
        }
        std::string value;
        assert(db->Get("name", &value).ok());
        assert(value == "Alice");
        delete db;
    }

    // 重新打开，验证持久化
    {
        minidb::DB* db;
        assert(minidb::DB::Open(options, dbname, &db).ok());

        std::string value;
        assert(db->Get("name", &value).ok());
        assert(value == "Alice");
        assert(db->Get("age", &value).ok());
        assert(value == "20");

        assert(db->Delete("age").ok());
        assert(db->Get("age", &value).IsNotFound());
        delete db;
    }

    // 再次打开，验证删除也持久化了
    {
        minidb::DB* db;
        assert(minidb::DB::Open(options, dbname, &db).ok());

        std::string value;
        assert(db->Get("age", &value).IsNotFound());
        assert(db->Get("name", &value).ok());
        assert(value == "Alice");
        delete db;
    }

    {
        minidb::DB* db;
        auto s = minidb::DB::Open(options, dbname, &db);
        if (!s.ok()) { std::cerr << "Third open failed: " << s.ToString() << std::endl; return 1; }

        // 插入一些用于扫描的数据
        db->Put("user_01", "Alice");
        db->Put("user_02", "Bob");
        db->Put("user_03", "Charlie");
        db->Delete("user_02"); // 测试墓碑跳过

        minidb::Iterator* it = db->NewIterator();
        
        std::cout << "--- Scan 开始 ---" << std::endl;
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            std::cout << it->key().ToString() << " -> " << it->value().ToString() << std::endl;
            }

        std::cout << "--- Scan 结束 ---" << std::endl;

        delete it;
        delete db;
    }

     // 测试 Compaction：写 50000 条，触发多次刷盘 + 合并
    {
        minidb::DB* db;
        auto s = minidb::DB::Open(options, dbname, &db);
        if (!s.ok()) { std::cerr << "Open for compaction failed" << std::endl; return 1; }

        for (int i = 0; i < 50000; ++i) {
            s = db->Put("ck_" + std::to_string(i), "cv_" + std::to_string(i));
            if (!s.ok()) { std::cerr << "Put failed at " << i << std::endl; return 1; }
        }

        // 验证数据完整性
        std::string value;
        for (int i = 0; i < 50000; i += 500) {
            s = db->Get("ck_" + std::to_string(i), &value);
            if (!s.ok() || value != "cv_" + std::to_string(i)) {
                std::cerr << "Compaction lost data at " << i << std::endl;
                return 1;
            }
        }

        delete db;
    }

    // 统计 SSTable 文件数量
    {
        int file_count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dbname)) {
            if (entry.path().extension() == ".sst") file_count++;
        }
        std::cout << "SSTable 文件数量: " << file_count << std::endl;
    }


    // 并发测试
    {
        minidb::DB* db;
        auto s = minidb::DB::Open(options, dbname, &db);
        if (!s.ok()) { std::cerr << "Concurrent open failed" << std::endl; return 1; }

        const int kNumWriters = 4;
        const int kNumReaders = 4;
        const int kOpsPerThread = 5000;   // 🌟 想加压就调大（比如 5000、10000）

        std::vector<std::thread> threads;
        std::atomic<int> errors{0};

        // 写线程
        for (int t = 0; t < kNumWriters; ++t) {
            threads.emplace_back([db, t, &errors, kOpsPerThread]() {
                for (int i = 0; i < kOpsPerThread; ++i) {
                    std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i);
                    if (!db->Put(key, "v").ok()) {
                        errors.fetch_add(1);
                        return;
                    }
                }
            });
        }

        // 读线程（和写线程并发跑）
        for (int t = 0; t < kNumReaders; ++t) {
            threads.emplace_back([db, t, kOpsPerThread]() {
                for (int i = 0; i < kOpsPerThread; ++i) {
                    std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i);
                    std::string value;
                    db->Get(key, &value);   // 允许读不到，只压测
                }
            });
        }

        for (auto& th : threads) th.join();

        if (errors.load() > 0) {
            std::cerr << "Concurrent errors: " << errors.load() << std::endl;
            return 1;
        }
        std::cout << "Concurrent test passed (" << kNumWriters << " writers + "
                  << kNumReaders << " readers, " << kOpsPerThread << " ops each)" << std::endl;

        delete db;
    }

    std::filesystem::remove_all(dbname);
    std::cout << "test_db passed" << std::endl;
    return 0;
}
