cmake_minimum_required(VERSION 3.14)
project(OrangeLi_Http_Server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

# 自动搜索头文件（关键）
include_directories(${PROJECT_SOURCE_DIR}/include/httpserver)

# 输出目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# ==========================================
# 1. CORE 核心模块
# ==========================================
file(GLOB_RECURSE CORE_SOURCES
    ${PROJECT_SOURCE_DIR}/src/core/*.cpp
)
add_library(core_lib STATIC ${CORE_SOURCES})
target_link_libraries(core_lib pthread)

# ==========================================
# 2. HTTP 模块（排除 database.cpp！）
# ==========================================
file(GLOB_RECURSE HTTP_SOURCES
    ${PROJECT_SOURCE_DIR}/src/application/http/*.cpp
)

# 移除不存在的 database.cpp
list(REMOVE_ITEM HTTP_SOURCES
    ${PROJECT_SOURCE_DIR}/src/application/http/database.cpp
)

add_library(http_lib STATIC ${HTTP_SOURCES})
target_link_libraries(http_lib core_lib pthread)

# ==========================================
# 3. 主程序
# ==========================================
add_executable(${PROJECT_NAME}
    ${PROJECT_SOURCE_DIR}/src/http_server_example_demo.cpp
)
target_link_libraries(${PROJECT_NAME}
    core_lib
    http_lib
    pthread
    sqlite3
)

# ==========================================
# 单元测试
# ==========================================
add_executable(test_core test/test_core.cpp)
target_link_libraries(test_core core_lib pthread)

add_executable(test_http test/test_http.cpp)
target_link_libraries(test_http core_lib http_lib pthread)