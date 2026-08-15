cmake_minimum_required(VERSION 3.20)
project(project_name LANGUAGES CXX)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/sylar)

include (sylar/cmake/utils.cmake)

set(CMAKE_VERBOSE_MAKEFILE ON)

#find_package(Protobuf REQUIRED)
#if(Protobuf_FOUND)
#    include_directories(${Protobuf_INCLUDE_DIRS})
#endif()
find_package(OpenSSL REQUIRED)

set(LIB_SRC
        template/my_module.cc
    )

#PROTOBUF_GENERATE_CPP(PB_SRCS PB_HDRS sylar/test.proto)
#list(APPEND LIB_SRC ${PB_SRCS})
#
#message(STATUS ${LIB_SRC})

add_library(project_name SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(project_name)
target_compile_features(project_name PUBLIC cxx_std_20)
set_target_properties(project_name PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(project_name PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/sylar
)
target_compile_options(project_name PRIVATE
    -Wall
    -Werror
    -Wno-unused-function
    -Wno-builtin-macro-redefined
)
target_link_libraries(project_name PRIVATE OpenSSL::SSL OpenSSL::Crypto)

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
