#include "minidb/sstable.h"
#include "minidb/coding.h"
#include <cstdio>

namespace minidb {

SSTableBuilder::~SSTableBuilder() {
    Close();
}

Status SSTableBuilder::Open(const std::string& path) {
    path_ = path;
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_.is_open()) return Status::IOError("Failed to open SSTable for writing");

    char zero[8] = {0};
    file_.write(zero, 8);
    count_ = 0;
    return Status::OK();
}

Status SSTableBuilder::Add(const Slice& key, ValueType type, const Slice& value) {
    char type_byte = static_cast<char>(type);
    file_.write(&type_byte, 1);

    char len_buf[4];
    EncodeFixed32(len_buf, static_cast<uint32_t>(key.size()));
    file_.write(len_buf, 4);
    file_.write(key.data(), key.size());

    EncodeFixed32(len_buf, static_cast<uint32_t>(value.size()));
    file_.write(len_buf, 4);
    file_.write(value.data(), value.size());

    count_++;
    return Status::OK();
}

Status SSTableBuilder::Finish() {
    if (!file_.is_open()) return Status::IOError("SSTable not open");
    file_.flush();

    file_.seekp(0, std::ios::beg);
    char count_buf[8];
    EncodeFixed64(count_buf, count_);
    file_.write(count_buf, 8);
    file_.flush();
    return Status::OK();
}

Status SSTableBuilder::Close() {
    if (file_.is_open()) file_.close();
    return Status::OK();
}

Status SSTableReader::Open(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return Status::IOError("Failed to open SSTable for reading");

    char count_buf[8];
    file.read(count_buf, 8);
    if (file.gcount() < 8) return Status::Corruption("SSTable file too short");

    uint64_t count = DecodeFixed64(count_buf);

    for (uint64_t i = 0; i < count; i++) {
        char type_byte;
        file.read(&type_byte, 1);
        if (file.gcount() < 1) return Status::Corruption("SSTable truncated at type");

        char len_buf[4];
        file.read(len_buf, 4);
        if (file.gcount() < 4) return Status::Corruption("SSTable truncated at key_len");
        uint32_t key_len = DecodeFixed32(len_buf);

        std::string key(key_len, '\0');
        file.read(&key[0], key_len);
        if (file.gcount() < (std::streamsize)key_len) return Status::Corruption("SSTable truncated at key");

        file.read(len_buf, 4);
        if (file.gcount() < 4) return Status::Corruption("SSTable truncated at value_len");
        uint32_t value_len = DecodeFixed32(len_buf);

        std::string value(value_len, '\0');
        file.read(&value[0], value_len);
        if (file.gcount() < (std::streamsize)value_len) return Status::Corruption("SSTable truncated at value");

        SSTableEntry entry;
        entry.type = static_cast<ValueType>(type_byte);
        entry.value = std::move(value);
        table_[std::move(key)] = std::move(entry);
    }
    return Status::OK();
}

Status SSTableReader::Get(const Slice& key, std::string* value) {
    auto it = table_.find(key.ToString());
    if (it == table_.end()) return Status::NotFound("key not found");
    if (it->second.type == kTypeDeletion) return Status::Deleted("key deleted"); // 🌟 关键！
    *value = it->second.value;
    return Status::OK();
}

Status SSTableReader::Close() {
    return Status::OK();
}

class SSTableIterator : public Iterator {
public:
    explicit SSTableIterator(const std::map<std::string, SSTableEntry>* table)
        : table_(table), iter_(table->begin()) {}

    bool Valid() const override { return iter_ != table_->end(); }
    void SeekToFirst() override { iter_ = table_->begin(); }
    void Seek(const Slice& target) override { iter_ = table_->lower_bound(target.ToString()); }
    void Next() override { ++iter_; }
    Slice key() const override { return Slice(iter_->first); }
    Slice value() const override { return Slice(iter_->second.value); }
    Status status() const override { return Status::OK(); }
    

private:
    const std::map<std::string, SSTableEntry>* table_;
    std::map<std::string, SSTableEntry>::const_iterator iter_;
};

Iterator* SSTableReader::NewIterator() {
    return new SSTableIterator(&table_);
}

} // namespace minidb