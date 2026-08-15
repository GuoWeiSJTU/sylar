cmake_minimum_required(VERSION 3.20)
project(sylar VERSION 2.0.0 LANGUAGES C CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/yaml-cpp)
#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/jsoncpp-1.8.4)
#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/sqlite-3.28.0)
#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/tinyxml2-7.0.1)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include (cmake/utils.cmake)

set(CMAKE_VERBOSE_MAKEFILE ON)

option(BUILD_TEST "Build Sylar test and example targets" OFF)
include(CTest)
if(BUILD_TEST)
    set(BUILD_TESTING ON CACHE BOOL "Enable CTest targets" FORCE)
endif()
if(BUILD_TESTING)
    set(BUILD_TEST ON CACHE BOOL "Build Sylar test and example targets" FORCE)
endif()

#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart)
#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/yaml-cpp/include)
#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/jsoncpp-1.8.4/include)
#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/sqlite-3.28.0)
#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdpart/tinyxml2-7.0.1)

find_package(MySQL REQUIRED)
find_package(Protobuf)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

set(LIB_SRC
    sylar/address.cc
    sylar/bytearray.cc
    sylar/config.cc
    sylar/db/fox_thread.cc
    sylar/db/mysql.cc
    sylar/db/sqlite3.cc
    sylar/ds/bitmap.cc
    sylar/ds/roaring_bitmap.cc
    sylar/ds/roaring.c
    sylar/ds/util.cc
    sylar/email/email.cc
    sylar/email/smtp.cc
    sylar/env.cc
    sylar/daemon.cc
    sylar/http/http.cc
    sylar/http/http_connection.cc
    sylar/http/async_http_session.cc
    sylar/http/async_websocket_session.cc
    sylar/http/http_parser.cc
    sylar/http/http_session.cc
    sylar/http/http_server.cc
    sylar/http/servlet.cc
    sylar/http/servlets/config_servlet.cc
    sylar/http/servlets/status_servlet.cc
    sylar/http/session_data.cc
    sylar/http/ws_session.cc
    sylar/http/ws_server.cc
    sylar/http/ws_servlet.cc
    sylar/log.cc
    sylar/mutex.cc
    sylar/protocol.cc
    sylar/rock/rock_protocol.cc
    sylar/rock/async_rock_session.cc
    sylar/rock/rock_server.cc
    sylar/socket.cc
    sylar/stream.cc
    sylar/streams/socket_stream.cc
    sylar/streams/zlib_stream.cc
    sylar/tcp_server.cc
    sylar/timer.cc
    sylar/thread.cc
    sylar/util.cc
    sylar/util/crypto_util.cc
    sylar/util/json_util.cc
    sylar/util/hash_util.cc
    sylar/application.cc
    sylar/coroutine/executor.cc
    sylar/coroutine/reactor.cc
    sylar/coroutine/timer_queue.cc
    sylar/net/async_socket.cc
    sylar/net/async_tcp_server.cc
    )

ragelmaker(sylar/http/http11_parser.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/sylar/http)
ragelmaker(sylar/http/httpclient_parser.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/sylar/http)
ragelmaker(sylar/uri.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/sylar)

# The nameserver protobuf module is optional and is not part of the coroutine
# runtime library.
#PROTOBUF_GENERATE_CPP(PB_SRCS PB_HDRS sylar/ns/ns_protobuf.proto)
#message("****${PB_SRCS}***${PB_HDRS}****")
#list(APPEND LIB_SRC ${PB_SRCS})
#
#message(STATUS ${LIB_SRC})

set_source_files_properties(sylar/ds/roaring.c PROPERTIES
    COMPILE_OPTIONS "-Wno-error=use-after-free;-Wno-error=unused-but-set-variable")

add_library(sylar SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(sylar)

target_compile_features(sylar
    PUBLIC cxx_std_20
    PRIVATE c_std_11
)
set_target_properties(sylar PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(sylar
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
        ${MYSQL_INCLUDE_DIR}
        ${Protobuf_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIR}
)
target_compile_options(sylar PRIVATE
    -O3
    -ggdb
    -Wall
    -Wno-deprecated
    -Werror
    -Wno-unused-function
    -Wno-builtin-macro-redefined
    -Wno-deprecated-declarations
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(sylar PRIVATE -rdynamic)
endif()

set(SYLAR_DEPENDENCIES
    Threads::Threads
    dl
    yaml-cpp
    jsoncpp
    ZLIB::ZLIB
    OpenSSL::SSL
    OpenSSL::Crypto
    $<$<BOOL:${Protobuf_FOUND}>:protobuf::libprotobuf>
    event
    mysqlclient
    sqlite3
    tinyxml2
    jemalloc
)
target_link_libraries(sylar PUBLIC ${SYLAR_DEPENDENCIES})
#add_library(sylar_static STATIC ${LIB_SRC})
#SET_TARGET_PROPERTIES (sylar_static PROPERTIES OUTPUT_NAME "sylar")

set(LIBS
        sylar
        )

set(SYLAR_INTEGRATION_TESTS test_http_server)

set(SYLAR_SLOW_TESTS test_thread test_bitmap)
set(SYLAR_LEGACY_TESTS)
set(SYLAR_ENVIRONMENT_TESTS
    test_config
    test_uri
)
sylar_add_executable(test_util "tests/test_util.cc" sylar "${LIBS}")
sylar_add_executable(test_array "tests/test_array.cc" sylar "${LIBS}")
sylar_add_executable(test_coroutine "tests/test_coroutine.cc" sylar "${LIBS}")
if(BUILD_TEST)
sylar_add_executable(test_config "tests/test_config.cc" sylar "${LIBS}")
sylar_add_executable(test_thread "tests/test_thread.cc" sylar "${LIBS}")
sylar_add_executable(test_address "tests/test_address.cc" sylar "${LIBS}")
sylar_add_executable(test_bytearray "tests/test_bytearray.cc" sylar "${LIBS}")
sylar_add_executable(test_http "tests/test_http.cc" sylar "${LIBS}")
sylar_add_executable(test_http_parser "tests/test_http_parser.cc" sylar "${LIBS}")
sylar_add_executable(echo_server "examples/echo_server.cc" sylar "${LIBS}")
sylar_add_executable(test_http_server "tests/test_http_server.cc" sylar "${LIBS}")
sylar_add_executable(test_uri "tests/test_uri.cc" sylar "${LIBS}")
sylar_add_executable(my_http_server "samples/my_http_server.cc" sylar "${LIBS}")
sylar_add_executable(test_env "tests/test_env.cc" sylar "${LIBS}")

endif()
sylar_add_executable(test_crypto "tests/test_crypto.cc" sylar "${LIBS}")
sylar_add_executable(test_bitmap "tests/test_bitmap.cc" sylar "${LIBS}")

set(ORM_SRCS
    sylar/orm/table.cc
    sylar/orm/column.cc
    sylar/orm/index.cc
    sylar/orm/orm.cc
    sylar/orm/util.cc
    )
sylar_add_executable(bin_sylar "sylar/main.cc" sylar "${LIBS}")
set_target_properties(bin_sylar PROPERTIES OUTPUT_NAME "sylar")

#add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/orm_out)
#set(OLIBS ${LIBS} orm_data)
#sylar_add_executable(test_orm "tests/test_orm.cc" orm_data "${OLIBS}")

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
