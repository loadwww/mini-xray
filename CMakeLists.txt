cmake_minimum_required(VERSION 3.20)
project(mini_xray LANGUAGES CXX VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(mxr
    src/main.cpp
    src/common/version.cpp
    src/cli/cli.cpp
    src/config/config.cpp
    src/common/filewriter.cpp
)
target_include_directories(mxr PRIVATE src third_party)

