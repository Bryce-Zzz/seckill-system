# Findlibuv.cmake — 创建 libuv::uv_a CMake imported target
# libuv 系统包没有 CMake config，手动创建兼容目标
find_path(LIBUV_INCLUDE_DIR NAMES uv.h PATH_SUFFIXES uv)
find_library(LIBUV_LIBRARY NAMES uv)

if(NOT TARGET libuv::uv_a)
    add_library(libuv::uv_a STATIC IMPORTED)
    set_target_properties(libuv::uv_a PROPERTIES
        IMPORTED_LOCATION "${LIBUV_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBUV_INCLUDE_DIR}"
    )
endif()

# 提供标准变量（可选）
set(LIBUV_FOUND TRUE)
