cmake_minimum_required(VERSION 3.10)
project(predictionmarkets_rec)

# 默认 Release 构建：自动带 -DNDEBUG，使 brpc 的 LOG(DEBUG) 塌缩为 VERBOSE，
# 从而被日志级别阈值屏蔽（线上不落 debug）。本地调试保留 DEBUG：cmake -DCMAKE_BUILD_TYPE=Debug ..
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -pthread")

include_directories(src)
include_directories(third_party)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

find_package(CURL REQUIRED)
find_package(Protobuf REQUIRED)

set(PROTO_FILE ${CMAKE_SOURCE_DIR}/src/proto/rec.proto)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILE})

message(STATUS "PROTO_SRCS = ${PROTO_SRCS}")
message(STATUS "PROTO_HDRS = ${PROTO_HDRS}")

get_filename_component(PROTO_DIR ${PROTO_FILE} DIRECTORY)
get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)

set(PROTO_HDRS ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.h)
set(PROTO_SRCS ${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.cc)

# 自动生成命令
add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${BRPC_SRCS} ${BRPC_HDRS}
    COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
        --proto_path ${PROTO_DIR}
        --cpp_out ${CMAKE_CURRENT_BINARY_DIR}
        --brpc_out ${CMAKE_CURRENT_BINARY_DIR}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating protobuf + brpc code"
)

file(GLOB_RECURSE SRC_FILES
    src/common/*.cpp
    src/rec/*.cpp
    src/server/*.cpp
    src/fetcher/*.cpp
    src/data/*.cpp
    third_party/murmurhash3/*.cpp 
    main.cpp
)

add_executable(predictionmarkets_rec
    ${SRC_FILES}
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

include_directories(${CURL_INCLUDE_DIRS})
include_directories(${PROTOBUF_INCLUDE_DIRS})

target_link_libraries(predictionmarkets_rec
    brpc
    gflags
    ${PROTOBUF_LIBRARIES}
    leveldb
    pthread
    dl
    ssl
    crypto
    ${CURL_LIBRARIES}
)
