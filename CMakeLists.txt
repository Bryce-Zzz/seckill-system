cmake_minimum_required(VERSION 3.15)
project(seckill-system VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -pthread")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# 可选：启用 AddressSanitizer
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    message(STATUS "AddressSanitizer ENABLED")
endif()

# ==================== 依赖库 ====================

# hiredis 垫片 + libuv CMake target（用户手写 Find*.cmake）
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# libuv CMake target（必须在 redis++ 之前，让 redis++ 找到 libuv::uv_a）
find_package(libuv)

# redis++ (自动包含 hiredis 传递依赖)
find_package(redis++ REQUIRED)

# 修复 redis++::redis++ target 缺失 IMPORTED_LOCATION
set_target_properties(redis++::redis++ PROPERTIES
    IMPORTED_LOCATION "/usr/local/lib/libredis++.so.1.3.15"
)

# libhv
find_library(LIBHV hv REQUIRED)

# yaml-cpp
find_library(YAMLCPP yaml-cpp REQUIRED)

# MySQL client
find_library(MYSQL_CLIENT mysqlclient REQUIRED)

# OpenSSL (for MD5 in token generation)
find_package(OpenSSL REQUIRED)

# ==================== 头文件目录 ====================
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    /usr/local/include
)

# ==================== 源文件 ====================
set(SOURCES
    src/main.cpp
    src/common/config.cpp
    src/common/logger.cpp
    src/common/metrics.cpp
    src/server/http_server.cpp
    src/service/redis_service.cpp
    src/service/rate_limiter.cpp
    src/service/mysql_service.cpp
    src/service/order_processor.cpp
    src/service/timeout_checker.cpp
    src/handler/seckill_handler.cpp
    src/handler/order_handler.cpp
    src/handler/admin_handler.cpp
    src/handler/health_handler.cpp
    src/handler/metrics_handler.cpp
    src/handler/sse_handler.cpp
    # MQ 消息队列模块（Redis Stream 实现）
    src/mq/RedisStreamMQ.cpp
)

# ==================== 可执行文件 ====================
add_executable(seckill-server ${SOURCES})

# ==================== 链接库 ====================
target_link_libraries(seckill-server
    PRIVATE
    redis++::redis++  # 👈 屠龙刀：自动链接 hiredis
    ${LIBHV}
    ${YAMLCPP}
    ${MYSQL_CLIENT}
    OpenSSL::Crypto   # MD5 摘要
    pthread
    dl
)

# ==================== 安装 ====================
install(TARGETS seckill-server DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/seckill)
install(DIRECTORY lua/ DESTINATION share/seckill/lua)
install(DIRECTORY web/ DESTINATION share/seckill/web)
install(DIRECTORY scripts/ DESTINATION share/seckill/scripts)
