cmake_minimum_required(VERSION 3.20)
project(drogon-blueprint VERSION 0.1.0 LANGUAGES CXX)

# C++20 required for coroutines (PERF-04)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Release build optimizations
# ---------------------------------------------------------------------------
# -O3:           Full optimization (loop vectorization, inlining, etc.)
# -march=native: Tune for the build machine's CPU (SSE4.2, AVX2, etc.)
# -flto:         Link-Time Optimization across all translation units
# -DNDEBUG:      Strip assert() calls (CMake adds this for Release already)
# ---------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(RELEASE_CXX_FLAGS "-O3 -march=native -flto")
    set(RELEASE_LINKER_FLAGS "-flto")

    string(APPEND CMAKE_CXX_FLAGS_RELEASE " ${RELEASE_CXX_FLAGS}")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " ${RELEASE_LINKER_FLAGS}")

    if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        string(APPEND CMAKE_CXX_FLAGS_RELWITHDEBINFO " ${RELEASE_CXX_FLAGS}")
        string(APPEND CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO " ${RELEASE_LINKER_FLAGS}")
    endif()

    message(STATUS "[Release] CXX flags:    ${CMAKE_CXX_FLAGS_RELEASE}")
    message(STATUS "[Release] Linker flags: ${CMAKE_EXE_LINKER_FLAGS_RELEASE}")
endif()

# ---------------------------------------------------------------------------
# mimalloc -- high-performance allocator (optional, enabled via USE_MIMALLOC)
# ---------------------------------------------------------------------------
option(USE_MIMALLOC "Link mimalloc as the global allocator (vcpkg)" ON)

if(USE_MIMALLOC)
    find_package(mimalloc CONFIG QUIET)
    if(mimalloc_FOUND)
        message(STATUS "[Allocator] mimalloc found -- linking as global override")
        set(MIMALLOC_AVAILABLE TRUE)
    else()
        message(WARNING
            "[Allocator] mimalloc NOT found via find_package.\n"
            "  Install via vcpkg:  vcpkg install mimalloc[override]\n"
            "  Or use LD_PRELOAD at runtime:  LD_PRELOAD=/usr/lib/libmimalloc.so ./drogon-blueprint\n"
            "  Building WITHOUT mimalloc link-time integration."
        )
        set(MIMALLOC_AVAILABLE FALSE)
    endif()
endif()

# Find vcpkg-managed packages
find_package(Drogon CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)

# Collect source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Main executable
add_executable(${PROJECT_NAME} main.cpp ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Drogon::Drogon
    jwt-cpp::jwt-cpp
)

# Link mimalloc override (replaces malloc/free/new/delete globally)
# Use generator expression to pick mimalloc-static if it exists (vcpkg default),
# otherwise fall back to the shared 'mimalloc' target.
if(USE_MIMALLOC AND MIMALLOC_AVAILABLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        $<IF:$<TARGET_EXISTS:mimalloc-static>,mimalloc-static,mimalloc>
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE MIMALLOC_ENABLED=1)
endif()

# Enable coroutines for GCC (required even with C++20 on GCC < 14)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE "-fcoroutines")
endif()
