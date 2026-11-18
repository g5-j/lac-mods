cmake_minimum_required(VERSION 3.22)
project(lac_admin_give LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "This plugin is built only for Android arm64-v8a")
endif()
add_library(lac_admin_give SHARED
    admin_give.cpp
    And64InlineHook.cpp
)
target_include_directories(lac_admin_give PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(lac_admin_give PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(lac_admin_give PRIVATE log dl)
