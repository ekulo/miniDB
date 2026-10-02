#include "minidb/db.h"
#include "minidb/memtable.h"
#include "minidb/sstable.h"
#include "minidb/wal.h"
#include "minidb/coding.h"
#include "minidb/iterator.h"
#include <memory>
#include <vector>
#include <fstream>
#include <cstdio>
#include <filesystem>
#include <shared_mutex>
#include <thread>
#include <atomic>



namespace minidb {


class MergingIterator : public Iterator {
public:
    explicit MergingIterator(const std::vector<Iterator*>& children)
        : children_(children), current_(nullptr) {}

    ~MergingIterator() override {
        for (auto* child : children_) delete child;
    }

    bool Valid() const override { return current_ != nullptr; }

    void SeekToFirst() override {
        for (auto* child : children_) child->SeekToFirst();
        FindSmallest();
    }

    void Seek(const Slice& target) override {
        for (auto* child : children_) child->Seek(target);
        FindSmallest();
    }

    void Next() override {
        current_->Next();
        FindSmallest();
    }

    Slice key() const override { return current_->key(); }
    Slice value() const override { return current_->value(); }
    Status status() const override { return Status::OK(); }

private:
    void FindSmallest() {
        Iterator* smallest = nullptr;
        
        // 在所有子迭代器中找到 key 最小的那个
        for (auto* child : children_) {
            if (!child->Valid()) continue;
            if (smallest == nullptr || child->key().ToString() < smallest->key().ToString()) {
                smallest = child;
            }
        }
        
        current_ = smallest;
        
        // 处理重复 key 和墓碑（当前实现极简，先不做复杂的去重）
        if (current_ != nullptr && current_->value().size() == 0) {
            // 如果是空值（墓碑），直接跳过（递归调用 Next 直到找到有效数据）
            Next(); 
        }
    }

    std::vector<Iterator*> children_;
    Iterator* current_;
};

static Status ParseWALRecord(const Slice& record, ValueType* type,
                              Slice* key, Slice* value,
                              std::string* key_storage, std::string* value_storage) {
    const char* p = record.data();
    const char* limit = p + record.size();

    if (p >= limit) return Status::Corruption("WAL record empty");
    *type = static_cast<ValueType>(*p++);

    if (p + 4 > limit) return Status::Corruption("WAL record too short for key_len");
    uint32_t key_len = DecodeFixed32(p);
    p += 4;
    if (p + key_len > limit) return Status::Corruption("WAL key overflow");
    key_storage->assign(p, key_len);
    *key = Slice(*key_storage);
    p += key_len;

    if (p + 4 > limit) return Status::Corruption("WAL record too short for value_len");
    uint32_t value_len = DecodeFixed32(p);
    p += 4;
    if (p + value_len > limit) return Status::Corruption("WAL value overflow");
    value_storage->assign(p, value_len);
    *value = Slice(*value_storage);
    return Status::OK();
}

class DBImpl : public DB {
public:
    DBImpl(const Options& options, const std::string& dbname)
        : options_(options), dbname_(dbname),
          mem_(std::make_unique<MemTable>()),
          wal_(std::make_unique<WALWriter>()),
          last_sequence_(0), next_file_number_(1) {}



    Status Init() {
        namespace fs = std::filesystem;

        if (options_.create_if_missing) {
            std::error_code ec;
            fs::create_directories(dbname_, ec);
            if (ec) return Status::IOError("Failed to create DB directory");
        }

        std::string wal_path = dbname_ + "/wal.log";
        bool wal_exists = fs::exists(wal_path);

        if (!wal_exists && !options_.create_if_missing) {
            return Status::IOError("DB does not exist and create_if_missing is false");
        }

        // 加载所有 SSTable
        for (uint64_t i = 1; i <= 1000; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "/%06llu.sst", (unsigned long long)i);
            std::string path = dbname_ + buf;
            if (!fs::exists(path)) continue;

            auto reader = std::make_unique<SSTableReader>();
            Status s = reader->Open(path);
            if (!s.ok()) return s;
            tables_.push_back({path, i, std::move(reader)});
            next_file_number_ = i + 1;
        }

        // 重放 WAL
        if (wal_exists) {
            WALReader wal_reader;
            Status s = wal_reader.Open(wal_path);
            if (!s.ok()) return s;

            Slice record;
            std::string scratch;
            while (true) {
                s = wal_reader.ReadRecord(&record, &scratch);
                if (s.IsNotFound()) break;
                if (!s.ok()) return s;

                ValueType type;
                Slice key, value;
                std::string key_storage, value_storage;
                Status ps = ParseWALRecord(record, &type, &key, &value,
                                            &key_storage, &value_storage);
                if (!ps.ok()) return ps;

                last_sequence_++;
                mem_->Add(last_sequence_, type, key, value);
            }
            wal_reader.Close();
        }

        // 打开 WAL 用于追加
        Status s = wal_->Open(wal_path);
        if (!s.ok()) return s;

        bg_thread_ = std::thread(&DBImpl::BackgroundLoop, this);
        return Status::OK();
    }

    ~DBImpl() noexcept override {
        {
            std::unique_lock<std::mutex> lock(bg_mutex_);
            shutting_down_.store(true);
        }
        bg_cv_.notify_all();
        if (bg_thread_.joinable()) bg_thread_.join();
        if (wal_) wal_->Close();
    }

    void BackgroundLoop() {
        while (true) {
            std::unique_lock<std::mutex> lock(bg_mutex_);
            bg_cv_.wait(lock, [this] {
                return shutting_down_.load() || flush_requested_.load()|| compaction_requested_.load();
            });
            if (shutting_down_.load()) return;
            bool do_flush = flush_requested_.exchange(false);
            bool do_compact = compaction_requested_.exchange(false);
            lock.unlock();
            
            {
                std::unique_lock<std::shared_mutex> db_lock(mutex_);
                // 🌟 后台做刷盘（拿写锁，阻塞前端写）
                if (do_flush && mem_->ApproximateMemoryUsage() >= options_.memtable_size_limit) {
                    FlushMemTable();
                }
                if (do_compact && (int)tables_.size() >= options_.compaction_trigger) {
                    DoCompaction();
                }
            }
        }
    }

    Status Put(const Slice& key, const Slice& value) override {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (key.size() == 0) return Status::IOError("empty key");
        return WriteRecord(kTypeValue, key, value);
    }

    Status Delete(const Slice& key) override {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        if (key.size() == 0) return Status::IOError("empty key");
        return WriteRecord(kTypeDeletion, key, Slice());
    }

    Status Get(const Slice& key, std::string* value) override {
        // 1. 查 MemTable
        Status s = mem_->Get(key, value);
        if (s.ok()) return s;
        if (s.IsDeleted()) return Status::NotFound("key deleted"); // 🌟 墓碑对外统一表现为 NotFound
        if (!s.IsNotFound()) return s;

        // 2. 查 SSTable（从新到旧）
        for (auto it = tables_.rbegin(); it != tables_.rend(); ++it) {
            s = it->reader->Get(key, value);  
            if (s.ok()) return s;
            if (s.IsDeleted()) return Status::NotFound("key deleted"); // 🌟 墓碑对外统一表现为 NotFound
            if (!s.IsNotFound()) return s;
        }
        return Status::NotFound("key not found");
    }

        Iterator* NewIterator() override {
        std::vector<Iterator*> children;
        
        // 1. 加入 MemTable 的迭代器
        children.push_back(mem_->NewIterator());
        
        // 2. 加入所有 SSTable 的迭代器（从旧到新）
        for (auto& table : tables_) {
            children.push_back(table.reader->NewIterator());
        }
        
        return new MergingIterator(children);
    }

private:
    Status WriteRecord(ValueType type, const Slice& key, const Slice& value) {
        std::string record;
        record.push_back(static_cast<char>(type));

        char len_buf[4];
        EncodeFixed32(len_buf, static_cast<uint32_t>(key.size()));
        record.append(len_buf, 4);
        record.append(key.data(), key.size());

        EncodeFixed32(len_buf, static_cast<uint32_t>(value.size()));
        record.append(len_buf, 4);
        record.append(value.data(), value.size());

        Status s = wal_->AddRecord(Slice(record));
        if (!s.ok()) return s;

        last_sequence_++;
        mem_->Add(last_sequence_, type, key, value);

        if (mem_->ApproximateMemoryUsage() >= options_.memtable_size_limit) {
            {
                std::unique_lock<std::mutex> lock(bg_mutex_);
                flush_requested_.store(true);
            }
            bg_cv_.notify_one();
        }
        return Status::OK();
    }

    Status FlushMemTable() {
        if (mem_->ApproximateMemoryUsage() == 0) return Status::OK();

        char buf[32];
        snprintf(buf, sizeof(buf), "/%06llu.sst", (unsigned long long)next_file_number_++);
        std::string sst_path = dbname_ + buf;

        SSTableBuilder builder;
        Status s = builder.Open(sst_path);
        if (!s.ok()) return s;

        auto& tbl = mem_->table();
        auto it = tbl.Begin();
        while (it.Valid()) {
            const MemEntry& entry = it.entry();
            s = builder.Add(it.key(), entry.type, entry.value);
            if (!s.ok()) { builder.Close(); return s; }
            it.Next();
        }

        s = builder.Finish();
        builder.Close();
        if (!s.ok()) return s;

        uint64_t new_file_number = next_file_number_ - 1;
        auto reader = std::make_unique<SSTableReader>();
        s = reader->Open(sst_path);
        if (!s.ok()) return s;
        tables_.push_back({sst_path, new_file_number, std::move(reader)});

        mem_->Clear();

        // 重置 WAL（数据已经落到 SSTable 了）
        wal_->Close();
        std::string wal_path = dbname_ + "/wal.log";
        {
            std::ofstream ofs(wal_path, std::ios::trunc | std::ios::binary);
            ofs.close(); 
        }
        s = wal_->Open(wal_path);
        if (!s.ok()) return s;

                // 🌟 检查是否需要 Compaction
        if ((int)tables_.size() >= options_.compaction_trigger) {
            compaction_requested_.store(true);
        }
        return Status::OK();
    }


    Status DoCompaction() {
        if (tables_.size() < 2) return Status::OK();

        char buf[32];
        snprintf(buf, sizeof(buf), "/%06llu.sst",
                 (unsigned long long)next_file_number_++);
        std::string sst_path = dbname_ + buf;

        SSTableBuilder builder;
        Status s = builder.Open(sst_path);
        if (!s.ok()) return s;

        // 用 map 归并：从旧到新，后面覆盖前面
        std::map<std::string, SSTableEntry> merged;
        for (auto& f : tables_) {
            for (const auto& [key, entry] : f.reader->table()) {
                merged[key] = entry;
            }
        }

        // 输出到新 SSTable，跳过墓碑
        for (const auto& [key, entry] : merged) {
            if (entry.type == kTypeDeletion) continue;
            s = builder.Add(Slice(key), entry.type, Slice(entry.value));
            if (!s.ok()) { builder.Close(); return s; }
        }

        s = builder.Finish();
        builder.Close();
        if (!s.ok()) return s;

        // 删除旧文件
        for (auto& f : tables_) {
            std::remove(f.path.c_str());
        }
        tables_.clear();

        // 加载新文件
        auto reader = std::make_unique<SSTableReader>();
        s = reader->Open(sst_path);
        if (!s.ok()) return s;
        tables_.push_back({sst_path, next_file_number_ - 1, std::move(reader)});

        return Status::OK();
    }


    Options options_;
    std::string dbname_;
    std::unique_ptr<MemTable> mem_;
    std::unique_ptr<WALWriter> wal_;
        struct SSTableFile {
        std::string path;
        uint64_t file_number;
        std::unique_ptr<SSTableReader> reader;
    };
    std::vector<SSTableFile> tables_;
    SequenceNumber last_sequence_;
    uint64_t next_file_number_;
    mutable std::shared_mutex mutex_;  // 保护 mem_ 和 tables_
    std::thread bg_thread_;
    std::mutex bg_mutex_;
    std::condition_variable bg_cv_;
    std::atomic<bool> shutting_down_{false};
    std::atomic<bool> flush_requested_{false};
    std::atomic<bool> compaction_requested_{false};
};

Status DB::Open(const Options& options, const std::string& name, DB** dbptr) {
    auto* db = new DBImpl(options, name);
    Status s = db->Init();
    if (!s.ok()) {
        delete db;
        return s;
    }
    *dbptr = db;
    return Status::OK();
}




} // namespace minidb