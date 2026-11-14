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
    src/net/listener.cpp
    src/http/http_request.cpp
    src/proxy/route.cpp
    src/proxy/target.cpp
    src/proxy/server.cpp
)
target_include_directories(mxr PRIVATE src third_party third_party/cpp-httplib)
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

# T2.2 验证台：手写的最小 HTTP 服务端
add_executable(http_server_check
    tests/http_server_check.cpp
    src/net/socket.cpp
    src/net/listener.cpp
    src/http/http_request.cpp
)
target_include_directories(http_server_check PRIVATE src third_party)
target_link_libraries(http_server_check PRIVATE ws2_32)

# T2.3 验证台：同样的服务端，换成 cpp-httplib（header-only，只 include 即可）
add_executable(httplib_check
    tests/httplib_check.cpp
)
target_include_directories(httplib_check PRIVATE third_party third_party/cpp-httplib)
target_link_libraries(httplib_check PRIVATE ws2_32)

# T2.4 mock 上游：M3 起当"假 OpenAI"用，长期挂着的进程
add_executable(mock_upstream
    tests/mock_upstream.cpp
)
target_include_directories(mock_upstream PRIVATE third_party third_party/cpp-httplib)
target_link_libraries(mock_upstream PRIVATE ws2_32)

# T3.1 验证台：路由匹配（纯字符串，不联网）
add_executable(route_test
    tests/route_test.cpp
    src/proxy/route.cpp
    src/proxy/target.cpp
)
target_include_directories(route_test PRIVATE src third_party)
