# MiniDB

一个从零实现的、基于 LSM-Tree 架构的嵌入式 KV 存储引擎，使用现代 C++17 编写。

MiniDB 参考 LevelDB / RocksDB 的核心设计，实现了 WAL 预写日志、跳表内存表、SSTable 磁盘存储、多路归并迭代器、Compaction 合并清理、崩溃恢复以及读写并发控制。项目不含任何第三方依赖，纯手写底层数据结构与存储格式。

---

## 特性

- **持久化**：WAL 预写日志 + 磁盘 SSTable 分层存储
- **崩溃恢复**：重启后自动加载 SSTable 并重放 WAL
- **有序存储**：基于手写多层索引跳表（SkipList），天然有序
- **零拷贝**：全程使用 Slice 视图，避免字符串复制开销
- **内存池**：Arena 批量分配，减少 malloc / free 与内存碎片
- **数据校验**：CRC32 校验每条 WAL 记录，防止磁盘损坏
- **删除语义**：墓碑（Tombstone）机制 + 遮蔽过滤
- **空间回收**：Compaction 合并 SSTable，清理墓碑与历史版本
- **多路归并**：MergingIterator 统一遍历 MemTable 与所有 SSTable
- **并发**：`std::shared_mutex` 读写分离 + 后台线程异步刷盘

---

## 架构

```
+--------------------------------------------+
|               使用者 / CLI                  |
+--------------------------------------------+
|      DB API：Put / Get / Delete / Scan      |
+--------------------------------------------+
|   DBImpl：读写协调、恢复、后台刷盘、迭代器     |
+--------------------------------------------+
|   WAL    |   MemTable(SkipList)  |  Version |
+--------------------------------------------+
|   SSTable | TableCache | MergingIterator    |
+--------------------------------------------+
| Status | Slice | Arena | Coding | Iterator  |
+--------------------------------------------+
```

### 分层说明

| 层次 | 内容 | 职责 |
|------|------|------|
| 基础工具层 | `Status`、`Slice`、`Arena`、`Coding`、`Iterator` | 统一错误处理、内存管理、编解码、迭代器接口 |
| 存储核心层 | `WAL`、`MemTable`、`SSTable`、`Compaction` | 内存索引、持久化、合并清理 |
| 协调层 | `DBImpl` | 串联各模块，实现读写路径、恢复、并发控制 |

---

## 目录结构

```
miniDB/
├── CMakeLists.txt
├── README.md
├── include/minidb/
│   ├── arena.h          # Arena 内存池
│   ├── coding.h         # 编解码 + CRC32
│   ├── db.h             # DB 抽象接口 + Options
│   ├── iterator.h       # 迭代器抽象接口
│   ├── memtable.h       # 内存表
│   ├── minidb.h         # 伞形头文件
│   ├── skiplist.h       # 手写跳表
│   ├── slice.h          # 零拷贝字符串视图
│   ├── sstable.h        # 磁盘有序表
│   ├── status.h         # 错误码封装
│   └── wal.h            # 预写日志
├── src/
│   ├── db/
│   │   └── db_impl.cpp  # DBImpl 实现
│   ├── storage/
│   │   ├── memtable.cpp
│   │   ├── sstable.cpp
│   │   └── wal.cpp
│   └── util/
│       ├── coding.cpp
│       └── status.cpp
├── tests/
│   ├── test_db.cpp      # 端到端 + 并发压力测试
│   ├── test_status.cpp  # 工具层单元测试
│   └── test_wal.cpp     # WAL 单元测试
└── tools/
    └── main.cpp         # CLI 入口（占位）
```

---

## 核心模块

### WAL（预写日志）

每条记录格式：

```
[CRC32 (4字节)] [Length (4字节)] [Data (Length字节)]
```

- 写入时先写 WAL 再写 MemTable，保证崩溃可恢复
- 读取时校验 CRC32，截断记录返回 Corruption
- 每次 MemTable 刷盘后重置 WAL

### MemTable（内存表）

- 底层为手写跳表（SkipList），平均 O(log N) 插入与查找
- 节点内存由 Arena 统一分配，Arena 销毁时一次性释放
- Key / Value 均为 Slice 视图，数据本体存储在 Arena
- 删除操作写入 Tombstone（`kTypeDeletion`）

### SSTable（磁盘有序表）

最简文件格式：

```
[count (8字节)]
[key_len (4字节)] [key] [value_len (4字节)] [value] ... × count
```

- 数据按 key 有序写入
- 读取时全量加载到 `std::map<std::string, SSTableEntry>`
- 支持点查与迭代器遍历

### Compaction（合并）

- 触发条件：SSTable 文件数 ≥ `compaction_trigger`（默认 4）
- 策略：全量合并，把所有 SSTable 读入内存后归并
- 归并规则：同一 key 保留最新版本，跳过墓碑
- 合并完成后删除旧文件，写入新的 SSTable

### MergingIterator（多路归并）

- 合并 MemTable 迭代器与所有 SSTable 迭代器
- 每次 `Next()` 从所有子迭代器中选出最小 key
- 对上层提供统一的有序 KV 流视图

---

## 数据流

### Put

```
Put(key, value)
  -> 获取写锁
  -> 序列化 WAL 记录
  -> 写 WAL
  -> 写入 MemTable
  -> 检查内存阈值
    -> 若超限，通知后台线程刷盘
  -> 释放锁
```

### Get

```
Get(key)
  -> 获取读锁
  -> 查 MemTable
    -> 命中且为墓碑：返回 NotFound
    -> 命中且为正常值：返回
  -> 从新到旧遍历 SSTable
    -> 命中：返回
  -> 未命中：返回 NotFound
```

### Recovery

```
DB::Open
  -> 扫描目录加载所有 .sst 文件
  -> 打开 WAL
  -> 逐条重放 WAL 到 MemTable
  -> 启动后台线程
  -> 返回 DB 实例
```

---

## 并发模型

- **读写锁**：`std::shared_mutex` 保护 MemTable 与 SSTable 列表
  - 多个读操作（`Get`）可并发
  - 写操作（`Put` / `Delete`）与刷盘互斥
- **后台线程**：`std::thread` + `std::condition_variable`
  - 主线程写入 MemTable 超限后通知后台
  - 后台线程异步执行 FlushMemTable 与 DoCompaction
  - 通过 `std::atomic<bool>` 与条件变量协调
- **析构安全**：`~DBImpl()` 通过 `shutting_down_` 标志 + `join()` 保证后台线程退出

---

## 构建

环境要求：

- C++17 编译器（MSVC 2019+ / GCC 9+ / Clang 10+）
- CMake 3.16+

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

Windows + MSVC 环境：

```powershell
mkdir build
cd build
cmake ..
cmake --build .
```

---

## 测试

```bash
cd build
ctest -C Debug
```

或直接运行端到端测试（含并发压力测试）：

```bash
./Debug/test_db.exe
```

测试覆盖：

- **test_status**：Status / Slice / Coding 单元测试
- **test_wal**：WAL 写入、读取、CRC 校验、恢复
- **test_db**：
  - Put / Get / Delete 正确性
  - 崩溃恢复（关闭后重开，数据不丢）
  - MemTable 刷盘
  - Iterator / Scan 范围遍历
  - 墓碑过滤
  - Compaction 合并后数据完整性
  - **并发压力测试：4 写 + 4 读 × 5000 ops 无错误**

---

## 示例

```cpp
#include "minidb/db.h"

int main() {
    minidb::Options options;
    options.create_if_missing = true;

    minidb::DB* db;
    minidb::DB::Open(options, "./mydb", &db);

    // 写入
    db->Put("name", "Alice");
    db->Put("age", "20");

    // 读取
    std::string value;
    db->Get("name", &value);            // value == "Alice"

    // 删除
    db->Delete("age");

    // 范围扫描
    minidb::Iterator* it = db->NewIterator();
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        // it->key(), it->value()
    }
    delete it;
    delete db;
    return 0;
}
```

---

## 已知限制

- SSTable 读取时全量加载进内存，不支持按块读取（工业级需要 Block + Index + Bloom Filter）
- Compaction 为全量合并，未实现 Leveled / Size-Tiered 分级策略
- 未实现 MVCC / Snapshot / 事务
- WAL 未实现文件轮转，仅通过 truncate 重置
- 尚无 Manifest / Version 机制，文件列表通过扫描目录重建

---

## 后续规划

- [ ] Bloom Filter：减少无效 SSTable 查找
- [ ] Block 分块存储 + Index Block：支持大文件按需读取
- [ ] Block Cache（LRU）：缓存热点数据块
- [ ] Manifest / Version：SSTable 版本管理与原子切换
- [ ] MVCC + Snapshot：快照读与事务支持
- [ ] WriteBatch：原子批量写入
- [ ] 多线程 Compaction：后台并行合并

---

## 参考

- LevelDB: https://github.com/google/leveldb
- RocksDB: https://github.com/facebook/rocksdb
- SkipList 论文：*Skip Lists: A Probabilistic Alternative to Balanced Trees*
- LSM-Tree 论文：*The Log-Structured Merge-Tree (LSM-Tree)*

---

## License

个人学习项目，自由使用。