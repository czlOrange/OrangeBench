# ============================================================================
# CMakeLists.txt - 顶级构建配置
# ============================================================================

cmake_minimum_required(VERSION 3.14)
project(httpserver LANGUAGES CXX)

# ============================================================================
# 1. 编译选项
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-O0 -Wall -Wextra -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -Wextra -DNDEBUG")

# ============================================================================
# 2. 输出目录
# ============================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================================
# 3. 头文件目录
# ============================================================================
set(PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ============================================================================
# 4. 第三方库目录（GTest）
# ============================================================================
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(GTEST_INCLUDE_DIR ${THIRD_PARTY_DIR}/gtest/include)
set(GTEST_LIB_DIR ${THIRD_PARTY_DIR}/gtest/lib)

# ============================================================================
# 5. 源文件分组（按模块）
# ============================================================================

# -------------------- Net 模块（底层网络） --------------------
set(NET_SOURCES
    src/net/address.cpp
    src/net/socket.cpp
    src/net/socket_ops.cpp
    src/net/acceptor.cpp
    src/net/connector.cpp
)

# -------------------- Buffer 模块（内存缓冲区） --------------------
set(BUFFER_SOURCES
    src/tcp_connection/Buffer/buffer.cpp
    src/tcp_connection/Buffer/buffer_factory.cpp
    src/tcp_connection/Buffer/buffer_manager.cpp
    src/tcp_connection/Buffer/buffer_operator.cpp
    src/tcp_connection/Buffer/buffer_view.cpp
    src/tcp_connection/Buffer/connection_buffer_mgr.cpp
    src/tcp_connection/Buffer/memory_pool.cpp
)

# -------------------- Async 模块（异步调度） --------------------
set(ASYNC_SOURCES
    src/tcp_connection/Async/async_operation.cpp
    src/tcp_connection/Async/async_scheduler.cpp
    src/tcp_connection/Async/async_scheduler_impl.cpp
    src/tcp_connection/Async/async_task.cpp
    src/tcp_connection/Async/task_queue.cpp
)

# -------------------- IO 模块（IO 事件处理） --------------------
set(IO_SOURCES
    src/tcp_connection/IO/io_handler.cpp
    src/tcp_connection/IO/io_multiplexer.cpp
    src/tcp_connection/IO/epoll_dispatcher.cpp
)

# -------------------- Connection 模块（连接管理） --------------------
set(CONNECTION_SOURCES
    src/tcp_connection/Connection/connection_state.cpp
    src/tcp_connection/Connection/connection.cpp
    src/tcp_connection/Connection/connection_manager.cpp
)

# -------------------- TCP 模块（TCP 连接集群） --------------------
set(TCP_SOURCES
    src/tcp_connection/Tcp/tcp_connections.cpp
    src/tcp_connection/Tcp/tcp_server.cpp
    src/tcp_connection/Tcp/tcp_client.cpp
)

# -------------------- Event 模块（事件分发） --------------------
set(EVENT_SOURCES
    src/tcp_connection/Event/event_dispatcher.cpp
    src/tcp_connection/Event/event_subscriber.cpp
    src/tcp_connection/Event/connection_event_mgr.cpp
    src/tcp_connection/Event/event_loop.cpp
)

# ============================================================================
# 6. 创建各个模块的静态库
# ============================================================================

# 线程库（所有模块都需要）
find_package(Threads REQUIRED)

# -------------------- Net 模块（底层，无依赖） --------------------
add_library(httpserver_net STATIC ${NET_SOURCES})
target_include_directories(httpserver_net PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(httpserver_net PRIVATE Threads::Threads)

# -------------------- Buffer 模块（依赖 Net） --------------------
add_library(httpserver_buffer STATIC ${BUFFER_SOURCES})
target_include_directories(httpserver_buffer PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(httpserver_buffer PRIVATE 
    httpserver_net
    Threads::Threads
)

# -------------------- Async 模块（依赖 Buffer） --------------------
add_library(httpserver_async STATIC ${ASYNC_SOURCES})
target_include_directories(httpserver_async PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(httpserver_async PRIVATE 
    httpserver_buffer
    Threads::Threads
)

# -------------------- IO 模块（依赖 Async） --------------------
add_library(httpserver_io STATIC ${IO_SOURCES})
target_include_directories(httpserver_io PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(httpserver_io PRIVATE 
    httpserver_async
    Threads::Threads
)

# -------------------- Connection 模块（依赖 IO） --------------------
add_library(httpserver_connection STATIC ${CONNECTION_SOURCES})
target_include_directories(httpserver_connection PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(httpserver_connection PRIVATE 
    httpserver_io
    Threads::Threads
)

# -------------------- TCP 模块（依赖 Connection） --------------------
add_library(httpserver_tcp STATIC ${TCP_SOURCES})
target_include_directories(httpserver_tcp PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(httpserver_tcp PRIVATE 
    httpserver_connection
    Threads::Threads
)

# -------------------- Event 模块（依赖 TCP） --------------------
add_library(httpserver_event STATIC ${EVENT_SOURCES})
target_include_directories(httpserver_event PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(httpserver_event PRIVATE 
    httpserver_tcp
    Threads::Threads
)

# -------------------- 主库（聚合所有模块） --------------------
add_library(httpserver STATIC
    ${NET_SOURCES}
    ${BUFFER_SOURCES}
    ${ASYNC_SOURCES}
    ${IO_SOURCES}
    ${CONNECTION_SOURCES}
    ${TCP_SOURCES}
    ${EVENT_SOURCES}
)
target_include_directories(httpserver PUBLIC ${PROJECT_INCLUDE_DIR})
target_link_libraries(httpserver PRIVATE Threads::Threads)

# ============================================================================
# 7. 测试配置
# ============================================================================
option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)
    enable_testing()
    
    # 检查 GTest 是否存在
    if(EXISTS ${GTEST_INCLUDE_DIR})
        message(STATUS "Using bundled GTest from third_party")
        
        # -------------------- Net 模块测试 --------------------
        file(GLOB NET_TEST_SOURCES test/net/*.cpp)
        if(NET_TEST_SOURCES)
            foreach(TEST_SOURCE ${NET_TEST_SOURCES})
                get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
                add_executable(${TEST_NAME} ${TEST_SOURCE})
                target_include_directories(${TEST_NAME} PRIVATE
                    ${PROJECT_INCLUDE_DIR}
                    ${GTEST_INCLUDE_DIR}
                )
                target_link_directories(${TEST_NAME} PRIVATE ${GTEST_LIB_DIR})
                target_link_libraries(${TEST_NAME} PRIVATE
                    httpserver_net
                    gtest
                    gtest_main
                    Threads::Threads
                )
                add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
            endforeach()
        endif()
        
        # -------------------- Buffer 模块测试 --------------------
        file(GLOB BUFFER_TEST_SOURCES test/tcp_connection/Buffer/*.cpp)
        if(BUFFER_TEST_SOURCES)
            foreach(TEST_SOURCE ${BUFFER_TEST_SOURCES})
                get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
                add_executable(${TEST_NAME} ${TEST_SOURCE})
                target_include_directories(${TEST_NAME} PRIVATE
                    ${PROJECT_INCLUDE_DIR}
                    ${GTEST_INCLUDE_DIR}
                )
                target_link_directories(${TEST_NAME} PRIVATE ${GTEST_LIB_DIR})
                target_link_libraries(${TEST_NAME} PRIVATE
                    httpserver_buffer
                    gtest
                    gtest_main
                    Threads::Threads
                )
                add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
            endforeach()
        endif()
        
        # -------------------- Async 模块测试 --------------------
        file(GLOB ASYNC_TEST_SOURCES test/tcp_connection/Async/*.cpp)
        if(ASYNC_TEST_SOURCES)
            foreach(TEST_SOURCE ${ASYNC_TEST_SOURCES})
                get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
                add_executable(${TEST_NAME} ${TEST_SOURCE})
                target_include_directories(${TEST_NAME} PRIVATE
                    ${PROJECT_INCLUDE_DIR}
                    ${GTEST_INCLUDE_DIR}
                )
                target_link_directories(${TEST_NAME} PRIVATE ${GTEST_LIB_DIR})
                target_link_libraries(${TEST_NAME} PRIVATE
                    httpserver_async
                    gtest
                    gtest_main
                    Threads::Threads
                )
                add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
            endforeach()
        endif()
        
        # -------------------- IO 模块测试 --------------------
        file(GLOB IO_TEST_SOURCES test/tcp_connection/IO/*.cpp)
        if(IO_TEST_SOURCES)
            foreach(TEST_SOURCE ${IO_TEST_SOURCES})
                get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
                add_executable(${TEST_NAME} ${TEST_SOURCE})
                target_include_directories(${TEST_NAME} PRIVATE
                    ${PROJECT_INCLUDE_DIR}
                    ${GTEST_INCLUDE_DIR}
                )
                target_link_directories(${TEST_NAME} PRIVATE ${GTEST_LIB_DIR})
                target_link_libraries(${TEST_NAME} PRIVATE
                    httpserver_io
                    gtest
                    gtest_main
                    Threads::Threads
                )
                add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
            endforeach()
        endif()
        
        # -------------------- Connection 模块测试 --------------------
        file(GLOB CONNECTION_TEST_SOURCES test/tcp_connection/Connection/*.cpp)
        if(CONNECTION_TEST_SOURCES)
            foreach(TEST_SOURCE ${CONNECTION_TEST_SOURCES})
                get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
                add_executable(${TEST_NAME} ${TEST_SOURCE})
                target_include_directories(${TEST_NAME} PRIVATE
                    ${PROJECT_INCLUDE_DIR}
                    ${GTEST_INCLUDE_DIR}
                )
                target_link_directories(${TEST_NAME} PRIVATE ${GTEST_LIB_DIR})
                target_link_libraries(${TEST_NAME} PRIVATE
                    httpserver_connection
                    gtest
                    gtest_main
                    Threads::Threads
                )
                add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
            endforeach()
        endif()
        
        # -------------------- TCP 模块测试 --------------------
        file(GLOB TCP_TEST_SOURCES test/tcp_connection/Tcp/*.cpp)
        if(TCP_TEST_SOURCES)
            foreach(TEST_SOURCE ${TCP_TEST_SOURCES})
                get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
                add_executable(${TEST_NAME} ${TEST_SOURCE})
                target_include_directories(${TEST_NAME} PRIVATE
                    ${PROJECT_INCLUDE_DIR}
                    ${GTEST_INCLUDE_DIR}
                )
                target_link_directories(${TEST_NAME} PRIVATE ${GTEST_LIB_DIR})
                target_link_libraries(${TEST_NAME} PRIVATE
                    httpserver_tcp
                    gtest
                    gtest_main
                    Threads::Threads
                )
                add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
            endforeach()
        endif()
        
        # -------------------- Event 模块测试 --------------------
        file(GLOB EVENT_TEST_SOURCES test/tcp_connection/Event/*.cpp)
        if(EVENT_TEST_SOURCES)
            foreach(TEST_SOURCE ${EVENT_TEST_SOURCES})
                get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
                add_executable(${TEST_NAME} ${TEST_SOURCE})
                target_include_directories(${TEST_NAME} PRIVATE
                    ${PROJECT_INCLUDE_DIR}
                    ${GTEST_INCLUDE_DIR}
                )
                target_link_directories(${TEST_NAME} PRIVATE ${GTEST_LIB_DIR})
                target_link_libraries(${TEST_NAME} PRIVATE
                    httpserver_event
                    gtest
                    gtest_main
                    Threads::Threads
                )
                add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
            endforeach()
        endif()
        
    else()
        message(WARNING "GTest not found in third_party, tests will not be built")
    endif()
endif()

# ============================================================================
# 8. 打印配置信息
# ============================================================================
message(STATUS "")
message(STATUS "========================================")
message(STATUS "Build Configuration:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Include directory: ${PROJECT_INCLUDE_DIR}")
message(STATUS "")
message(STATUS "Modules (with dependencies):")
message(STATUS "  httpserver_net      (底层网络) - base")
message(STATUS "  httpserver_buffer   (内存缓冲区) - depends on net")
message(STATUS "  httpserver_async    (异步调度) - depends on buffer")
message(STATUS "  httpserver_io       (IO事件处理) - depends on async")
message(STATUS "  httpserver_connection (连接管理) - depends on io")
message(STATUS "  httpserver_tcp      (TCP连接集群) - depends on connection")
message(STATUS "  httpserver_event    (事件分发) - depends on tcp")
message(STATUS "  httpserver          (主库聚合) - all modules")
message(STATUS "========================================")