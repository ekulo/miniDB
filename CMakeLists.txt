cmake_minimum_required(VERSION 3.16)
project(minidb CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# 核心库
add_library(minidb_util
    src/util/status.cpp
    src/util/coding.cpp
    src/storage/wal.cpp
    src/storage/memtable.cpp
    src/storage/sstable.cpp
    src/db/db_impl.cpp
)

# 可执行文件
add_executable(minidb_cli tools/main.cpp)
target_link_libraries(minidb_cli minidb_util)

# 测试
enable_testing()

add_executable(test_status tests/test_status.cpp)
target_link_libraries(test_status minidb_util)
add_test(NAME test_status COMMAND test_status)

add_executable(test_wal tests/test_wal.cpp)
target_link_libraries(test_wal minidb_util)
add_test(NAME test_wal COMMAND test_wal)

add_executable(test_db tests/test_db.cpp)
target_link_libraries(test_db minidb_util)
add_test(NAME test_db COMMAND test_db)

# if(MSVC)
#     target_compile_options(minidb_util PRIVATE /fsanitize=address)
#     target_compile_options(test_db PRIVATE /fsanitize=address)
#     target_link_options(test_db PRIVATE /fsanitize=address)
# endif()