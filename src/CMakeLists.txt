# COPG open-source engine — CMake build for the Zygisk library (libspoof.so).
#
# The Android NDK toolchain is selected by the caller:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21
#   cmake --build build
cmake_minimum_required(VERSION 3.22)
project(copg_spoof CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(NOT ANDROID)
  message(FATAL_ERROR "This project builds for Android — use the NDK toolchain file")
endif()

# zygisk_module_entry is declared [[gnu::visibility("default")]] in
# include/zygisk.hpp, so it stays exported despite the hidden preset below.
add_library(spoof SHARED
    zygisk_module.cpp
    spoof/props.cpp
    spoof/cpu.cpp
    spoof/gpu.cpp
    common/strings.cpp
    common/config.cpp
    common/gpu.cpp
)

target_include_directories(spoof PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_compile_options(spoof PRIVATE -Wall -Wextra -O2)
target_link_libraries(spoof log)
