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
    src/net/socket.cpp
)
target_include_directories(mxr PRIVATE src third_party)
# Winsock2 不是 header-only，socket/connect/send/recv 的实现在 ws2_32.dll 里，必须显式链接
target_link_libraries(mxr PRIVATE ws2_32)

# T1.4 验证台（独立目标，不参与 mxr 本体）
add_executable(jsonl_test
    tests/jsonl_test.cpp
    src/store/jsonl_writer.cpp
    src/common/filewriter.cpp
)
target_include_directories(jsonl_test PRIVATE src third_party)

# T2.1 验证台：裸 socket 客户端
add_executable(socket_check
    tests/socket_check.cpp
    src/net/socket.cpp
)
target_include_directories(socket_check PRIVATE src third_party)
target_link_libraries(socket_check PRIVATE ws2_32)
