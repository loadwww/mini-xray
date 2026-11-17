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
    src/cli/replay.cpp
    src/config/config.cpp
    src/common/filewriter.cpp
    src/store/jsonl_writer.cpp
    src/net/socket.cpp
    src/net/listener.cpp
    src/http/http_request.cpp
    src/proxy/route.cpp
    src/proxy/target.cpp
    src/proxy/stream_bridge.cpp
    src/proxy/server.cpp
    src/parse/sse.cpp
    src/common/clock.cpp
    src/store/event.cpp
    src/store/redact.cpp
    src/store/recorder.cpp
    src/store/sse_run.cpp
    src/model/model.cpp
    src/parse/extract.cpp
    src/chain/link.cpp
    src/chain/superstep.cpp
    src/graph/mermaid.cpp
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

# T3.3 验证台：脱敏 + 异步记录器（不联网，但会写临时文件）
add_executable(recorder_test
    tests/recorder_test.cpp
    src/store/recorder.cpp
    src/store/event.cpp
    src/store/redact.cpp
    src/store/jsonl_writer.cpp
    src/common/filewriter.cpp
    src/common/clock.cpp
)
target_include_directories(recorder_test PRIVATE src third_party)

# T4.1 验证台：chunked 解码（纯字节操作，不联网）
add_executable(chunked_test
    tests/chunked_test.cpp
    src/parse/chunked.cpp
)
target_include_directories(chunked_test PRIVATE src third_party)

# T4.2 验证台：StreamBridge（纯线程，不联网）
add_executable(stream_bridge_test
    tests/stream_bridge_test.cpp
    src/proxy/stream_bridge.cpp
)
target_include_directories(stream_bridge_test PRIVATE src third_party)

# T4.3 验证台：SSE 解析状态机（纯字符串，不联网）
add_executable(sse_test
    tests/sse_test.cpp
    src/parse/sse.cpp
)
target_include_directories(sse_test PRIVATE src third_party)

# T4.4 验证台：落盘 → 重放 → 对账（不联网，会写 runs/replay_test_events.jsonl）
add_executable(replay_test
    tests/replay_test.cpp
    src/cli/replay.cpp
    src/store/sse_run.cpp
    src/parse/sse.cpp
    src/store/recorder.cpp
    src/store/event.cpp
    src/store/redact.cpp
    src/store/jsonl_writer.cpp
    src/common/filewriter.cpp
    src/common/clock.cpp
)
target_include_directories(replay_test PRIVATE src third_party)

# T5.1 验证台：执行图数据模型的 JSON 往返（纯数据，不联网）
add_executable(model_test
    tests/model_test.cpp
    src/model/model.cpp
)
target_include_directories(model_test PRIVATE src third_party)

# T5.2 验证台：协议字段提取（纯 JSON，不联网）
add_executable(extract_test
    tests/extract_test.cpp
    src/parse/extract.cpp
    src/model/model.cpp
    src/store/event.cpp
)
target_include_directories(extract_test PRIVATE src third_party)

# T5.3 验证台：建链（纯数据，会写 runs/link_fixture.jsonl）
add_executable(link_test
    tests/link_test.cpp
    src/chain/link.cpp
    src/parse/extract.cpp
    src/model/model.cpp
)
target_include_directories(link_test PRIVATE src third_party)

# T5.4 验证台：超步归并（纯数据，不联网）
add_executable(superstep_test
    tests/superstep_test.cpp
    src/chain/superstep.cpp
    src/chain/link.cpp
    src/parse/extract.cpp
    src/model/model.cpp
)
target_include_directories(superstep_test PRIVATE src third_party)

# T6.1 验证台：Mermaid 图输出（纯字符串，不联网、不写文件）
add_executable(mermaid_test
    tests/mermaid_test.cpp
    src/graph/mermaid.cpp
    src/model/model.cpp
)
target_include_directories(mermaid_test PRIVATE src third_party)
