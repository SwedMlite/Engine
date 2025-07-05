cmake_minimum_required(VERSION 3.16)
project(main)

if(ANDROID)
    set(BUILD_SHARED_LIBS ON CACHE BOOL "Force shared libraries" FORCE)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(LIB_TYPE "SHARED")
else()
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Force shared libraries" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(LIB_TYPE "EXECUTABLE")
endif()

set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)

set(CMAKE_CXX_STANDARD 20)

# download CPM.cmake
file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.38.3/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
        EXPECTED_HASH SHA256=cc155ce02e7945e7b8967ddfaff0b050e958a723ef7aad3766d368940cb15494
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

set(LIBS)

# Vulkan-Headers
CPMAddPackage("gh:KhronosGroup/Vulkan-Headers@1.4.309")
list(APPEND LIBS Vulkan-Headers)

# SDL3 & Target. TODO: fix usage SDL from CPM for Android
if(${LIB_TYPE} STREQUAL "SHARED")
    find_package(SDL3 REQUIRED CONFIG)
    list(APPEND LIBS SDL3::SDL3)

    add_library(${PROJECT_NAME} SHARED main.cpp)
else()
    CPMAddPackage("gh:libsdl-org/SDL#6e2d3c9")
    list(APPEND LIBS SDL3::SDL3)

    add_executable(${PROJECT_NAME} main.cpp)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBS})