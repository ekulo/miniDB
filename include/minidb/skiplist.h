#pragma once
#include "minidb/arena.h"
#include "minidb/slice.h"
#include "minidb/iterator.h" 
#include <cstdint>
#include <cstring>              // 🌟 memcmp / std::min 用
#include <algorithm>  
#include <vector>

namespace minidb {

// 用户提供比较器，因为 Slice 本身没有 < 运算符
struct SliceComparator {
    int operator()(const Slice& a, const Slice& b) const {
        int r = std::memcmp(a.data(), b.data(), std::min(a.size(), b.size()));
        if (r != 0) return r;
        if (a.size() < b.size()) return -1;
        if (a.size() > b.size()) return 1;
        return 0;
    }
};

template <typename Key, typename Value, typename Comparator = SliceComparator>
class SkipList {
private:
    struct Node {
        Key key;
        Value value;

        Node* Next(int level) {
            return *reinterpret_cast<Node**>(
                reinterpret_cast<char*>(this) + sizeof(Node) + level * sizeof(Node*));
        }
        void SetNext(int level, Node* node) {
            *reinterpret_cast<Node**>(
                reinterpret_cast<char*>(this) + sizeof(Node) + level * sizeof(Node*)) = node;
        }
    };

public:
    explicit SkipList(Arena* arena, Comparator cmp = Comparator(), int max_level = 12)
        : arena_(arena), cmp_(cmp), max_level_(max_level),
          head_(NewNode(Key{}, Value{}, max_level_)),
          current_level_(0), rng_state_(0x12345678) {}

    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;

    SkipList(SkipList&& other) noexcept
        : arena_(other.arena_), cmp_(other.cmp_), max_level_(other.max_level_),
          head_(other.head_), current_level_(other.current_level_),
          rng_state_(other.rng_state_) {
        other.head_ = nullptr;
    }


    void Insert(const Key& key, const Value& value) {
        std::vector<Node*> prev(max_level_ + 1);
        Node* x = FindGreaterOrEqual(key, prev.data());

        // key 已存在，更新 value
        if (x != nullptr && cmp_(x->key, key) == 0) {
            x->value = value;
            return;
        }

        int level = RandomLevel();
        if (level > current_level_) {
            for (int i = current_level_ + 1; i <= level; ++i) prev[i] = head_;
            current_level_ = level;
        }

        Node* new_node = NewNode(key, value, level);
        for (int i = 0; i <= level; ++i) {
            new_node->SetNext(i, prev[i]->Next(i));
            prev[i]->SetNext(i, new_node);
        }
    }

    bool Find(const Key& key, Value* value) const {
        Node* x = FindGreaterOrEqual(key, nullptr);
        if (x != nullptr && cmp_(x->key, key) == 0) {
            *value = x->value;
            return true;
        }
        return false;
    }


    bool empty() const { return head_->Next(0) == nullptr; }



    class SkipListIterator : public minidb::Iterator{
    public:
        explicit SkipListIterator(const SkipList* list,Node* node) : list_(list),node_(node) {}
        bool Valid() const { return node_ != nullptr; }
        void SeekToFirst() override { node_ = list_->head_->Next(0); }
        void Seek(const Slice& target) override {        
            SeekToFirst();
            while (Valid() && list_->cmp_(node_->key, target) < 0) {
                Next();
            } 
        }
        Slice  key() const override  { return node_->key; }
        Slice  value() const override { return node_->value.value; }
        void Next() override { node_ = node_ ? node_->Next(0) : nullptr; }
        Status status() const override { return Status::OK(); }
        const Value& entry() const { return node_->value; }
    

    private:
        const SkipList* list_;
        Node* node_;
    };

        // 重置跳表：在新 Arena 上重建 head_，清空所有节点
    void Reset(Arena* arena) {
        arena_ = arena;
        head_ = NewNode(Key{}, Value{}, max_level_); // 在新 Arena 上分配 head_
        current_level_ = 0;
        rng_state_ = 0x12345678;
    }

    
    SkipListIterator  Begin() const { return SkipListIterator(this,head_->Next(0)); }

    minidb::Iterator* NewIterator() const {
        return new SkipListIterator(this, head_->Next(0));
    }

private:
    Node* NewNode(const Key& key, const Value& value, int level) {
        char* mem = arena_->Allocate(sizeof(Node) + (level + 1) * sizeof(Node*));
        Node* node = new (mem) Node();
        node->key = key;
        node->value = value;
        for (int i = 0; i <= level; ++i) node->SetNext(i, nullptr);
        return node;
    }

    Node* FindGreaterOrEqual(const Key& key, Node** prev) const {
        Node* x = head_;
        int level = current_level_;
        while (true) {
            Node* next = x->Next(level);
            if (next != nullptr && cmp_(next->key, key) < 0) {
                x = next;
            } else {
                if (prev != nullptr) prev[level] = x;
                if (level == 0) return next;
                --level;
            }
        }
    }

    int RandomLevel() {
        int level = 0;
        while ((rng_state_ = rng_state_ * 1103515245 + 12345) & 0x80000000) {
            if (level < max_level_) ++level;
        }
        return level;
    }

    Arena* arena_;
    Comparator cmp_;
    int max_level_;
    Node* head_;
    int current_level_;
    uint32_t rng_state_;
};

} // namespace minidb