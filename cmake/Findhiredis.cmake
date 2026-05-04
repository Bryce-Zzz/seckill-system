# cmake/Findhiredis.cmake

# 1. 寻找头文件路径
find_path(HIREDIS_INCLUDE_DIR NAMES hiredis/hiredis.h)

# 2. 寻找库文件路径
find_library(HIREDIS_LIBRARY NAMES hiredis)

# 3. 调用标准宏处理 FOUND 状态
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(hiredis
    DEFAULT_MSG
    HIREDIS_LIBRARY
    HIREDIS_INCLUDE_DIR
)

# 4. 🌟 核心：构造 redis++ 渴望看到的 hiredis::hiredis 现代 Target
if(hiredis_FOUND AND NOT TARGET hiredis::hiredis)
    add_library(hiredis::hiredis UNKNOWN IMPORTED)
    set_target_properties(hiredis::hiredis PROPERTIES
        IMPORTED_LOCATION "${HIREDIS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_INCLUDE_DIR}"
    )
endif()
