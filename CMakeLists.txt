cmake_minimum_required(VERSION 3.20)
project(mini_xray LANGUAGES CXX VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 让 CMake 在 build/ 下产出 compile_commands.json：IDE/clangd 据此获得精确的 include 路径与宏
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(mxr
    src/main.cpp
    src/common/version.cpp
    src/cli/cli.cpp
    src/config/config.cpp
    src/common/filewriter.cpp
    src/store/jsonl_writer.cpp
)
target_include_directories(mxr PRIVATE src third_party)

# T1.4 验证台（独立目标，不参与 mxr 本体）
add_executable(jsonl_test
    tests/jsonl_test.cpp
    src/store/jsonl_writer.cpp
    src/common/filewriter.cpp
)
target_include_directories(jsonl_test PRIVATE src third_party)

