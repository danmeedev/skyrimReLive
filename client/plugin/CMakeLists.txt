cmake_minimum_required(VERSION 3.21)

project(
    SkyrimReLive
    VERSION 0.0.1
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# CommonLibSSE-NG options (must be set before FetchContent populates it).
# We do not need its test target; disabling removes the catch2 dep.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    CommonLibSSE
    GIT_REPOSITORY https://github.com/alandtse/CommonLibVR.git
    GIT_TAG        v4.14.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(CommonLibSSE)

# CommonLibSSE-NG defines add_commonlibsse_plugin in this file. It is only
# auto-included when consuming the installed package (find_package); when
# we pull via FetchContent we must include it ourselves.
include("${commonlibsse_SOURCE_DIR}/cmake/CommonLibSSE.cmake")

# Flatbuffers C++ runtime (header-only).
find_package(flatbuffers CONFIG REQUIRED)
# tomlplusplus (header-only, used for SkyrimReLive.toml parsing).
find_package(tomlplusplus CONFIG REQUIRED)

# Phase 0: declare an explicit compatible runtime instead of relying on
# Address Library (a Nexus-hosted data file by meh321). We don't hook any
# game functions yet, so version-independent address resolution isn't needed.
# When we hook game internals in Phase 1+, switch to USE_ADDRESS_LIBRARY and
# document the AL install in setup.ps1.
add_commonlibsse_plugin(${PROJECT_NAME}
    AUTHOR "SkyrimReLive contributors"
    COMPATIBLE_RUNTIMES 1.6.1170
    SOURCES
        src/Plugin.cpp
        src/Plugin.h
        src/Net.cpp
        src/Net.h
        src/Socket.cpp
        src/Socket.h
        src/Config.cpp
        src/Config.h
        src/Ghost.cpp
        src/Ghost.h
        src/Commands.cpp
        src/Commands.h
        src/Cell.cpp
        src/Cell.h
        src/Combat.cpp
        src/Combat.h
)

# Socket.cpp is the only TU that includes winsock2.h; CommonLib's REX/W32
# rejects pre-included Windows API surface, so this file must NOT pick up
# the project PCH (which transitively pulls in CommonLib).
set_source_files_properties(src/Socket.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

# CommonLibSSE-NG's headers (REL/Relocation.h, SKSE/SKSE.h) depend on its PCH
# (SKSE/Impl/PCH.h) being included first. The library uses this PCH for itself
# but the add_commonlibsse_plugin helper doesn't propagate it to plugin targets,
# so we wire it up here. We use src/PCH.h which wraps the upstream PCH and
# additionally hoists `using namespace std::literals` to global scope, needed
# by the generated __<Target>Plugin.cpp.
target_precompile_headers(${PROJECT_NAME}
    PRIVATE
        src/PCH.h
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    # vcpkg's tomlplusplus was built with a newer MSVC STL than our v143
    # toolset links against, so its precompiled .lib references symbols our
    # vcruntime doesn't have. Forcing header-only mode inlines the parser
    # into our TUs compiled with OUR toolset — the precompiled .lib is then
    # unused at link time.
    TOML_HEADER_ONLY=1
)

# Generated flatbuffer headers cross-include each other with sibling paths,
# so the proto/v1 directory itself needs to be a search root.
target_include_directories(${PROJECT_NAME} PRIVATE
    src/proto/v1
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ws2_32
    flatbuffers::flatbuffers
    tomlplusplus::tomlplusplus
)
