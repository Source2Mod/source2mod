cmake_minimum_required(VERSION 3.18)
project(source2mod C CXX ASM)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# Source files are UTF-8 (the author strings); MSVC would read them as the ANSI code page.
if (MSVC)
    add_compile_options(/utf-8)
endif ()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(SPDLOG_HEADER_ONLY ON CACHE BOOL "" FORCE)
set(FMT_WERROR OFF CACHE BOOL "" FORCE)

function(resolve_dep_internal OUT_VAR ENV_NAME)
    if (DEFINED ENV{${ENV_NAME}} AND NOT "$ENV{${ENV_NAME}}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{${ENV_NAME}}" _resolved)
        set(${OUT_VAR} "${_resolved}" PARENT_SCOPE)
        message(STATUS "${ENV_NAME} -> ${_resolved}")
    else ()
        message(FATAL_ERROR "${ENV_NAME} is not set")
    endif ()
endfunction()

resolve_dep_internal(SOURCE2MOD_SDK_DIR SOURCE2MOD_SDK)
resolve_dep_internal(METAMOD_DIR MMSOURCE_DEV)

# core build
set(SOURCE2MOD_CORE ON CACHE BOOL "" FORCE)
add_subdirectory(${SOURCE2MOD_SDK_DIR} ${CMAKE_BINARY_DIR}/source2mod-sdk)

# Hooks are KHook, metamod's own detour library: header-only here, the engine
# arrives through PLUGIN_SAVEVARS() and is handed on to Source2Mod's plugins.

add_subdirectory(vendor/dynlibutils)
add_subdirectory(vendor/spdlog)

# The libraries the core builds itself, one file each under makefiles/.
include(${CMAKE_CURRENT_LIST_DIR}/makefiles/maxminddb/maxminddb.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/makefiles/sqlite/sqlite.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/makefiles/breakpad/breakpad.cmake)

# CONFIGURE_DEPENDS: ifacecompat.py generate adds files to src/compat/gen,
# which a plain glob would not see until the next configure.
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
        src/*.cpp
        src/*.h
        src/*.hpp
)

# dynhooks/ is work in progress and still written against SourceHook's own
# assembler and page allocator, which left with SourceHook. Out of the build
# until it is ported to KHook's (khook/asm, khook/memory).
list(FILTER SOURCE_FILES EXCLUDE REGEX "/src/core/dynhooks/")

# src/core/net*.cpp build into source2mod-net (makefiles/net/net.cmake).
list(FILTER SOURCE_FILES EXCLUDE REGEX "/src/core/net(_[^/]*)?\\.cpp$")

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})
target_compile_definitions(source2mod-sdk PUBLIC SOURCE2MOD_CORE)

if (MSVC)
    # Windows.h reaches the core through the SDK and pluginmanager.h; without
    # this its min/max macros break every std::min/std::max after it.
    target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)
endif ()

if (DEFINED ENV{GITHUB_SHA_SHORT} AND NOT "$ENV{GITHUB_SHA_SHORT}" STREQUAL "")
    add_definitions(-DGITHUB_SHA="$ENV{GITHUB_SHA_SHORT}")
else ()
    add_definitions(-DGITHUB_SHA="Local")
endif ()

if (DEFINED ENV{SEMVER} AND NOT "$ENV{SEMVER}" STREQUAL "")
    add_definitions(-DSEMVER="$ENV{SEMVER}")
else ()
    add_definitions(-DSEMVER="Local")
endif ()

# plugin-metadata.json is the one place the core's name, author and version
# live, as for a native plugin: tools/version_gen.py turns it into
# version_gen.h (SEMVER / GITHUB_SHA_SHORT above go into the version).
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(VERSION_GEN_DIR "${CMAKE_BINARY_DIR}/versioning")

execute_process(
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/version_gen.py
                --metadata ${CMAKE_SOURCE_DIR}/plugin-metadata.json
                --output ${VERSION_GEN_DIR}/version_gen.h
        RESULT_VARIABLE VERSION_GEN_RESULT
)

if (NOT VERSION_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "version_gen.py failed (${VERSION_GEN_RESULT})")
endif ()

# The adapters that serve older interface revisions (src/compat/gen) must
# match the frozen revisions and the SDK the core is built against: a stale
# one would forward to methods that are not there any more.
execute_process(
        COMMAND ${CMAKE_COMMAND} -E env SOURCE2MOD_SDK=${SOURCE2MOD_SDK_DIR}
                ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/ifacecompat/ifacecompat.py check
        RESULT_VARIABLE IFACECOMPAT_RESULT
)

if (NOT IFACECOMPAT_RESULT EQUAL 0)
    message(FATAL_ERROR "src/compat/gen is stale: run tools/ifacecompat/ifacecompat.py generate")
endif ()

# Re-run cmake when the metadata changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/plugin-metadata.json)

target_include_directories(${PROJECT_NAME} PRIVATE ${VERSION_GEN_DIR})

# The core's own configs (core.json, permissions.json, databases.json) are
# registered with the shipped examples as their defaults: compiled in from the
# same files, so the defaults and the examples cannot drift apart.
set(CONFIG_EXAMPLES_DIR "${CMAKE_SOURCE_DIR}/configs/addons/source2mod/configs")
set(CONFIG_EXAMPLES
        Core=${CONFIG_EXAMPLES_DIR}/core.example.json
        Permissions=${CONFIG_EXAMPLES_DIR}/permissions.example.json
        Databases=${CONFIG_EXAMPLES_DIR}/databases.example.json
)

execute_process(
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tools/config_defaults_gen.py
                --output ${VERSION_GEN_DIR}/config_defaults_gen.h
                ${CONFIG_EXAMPLES}
        RESULT_VARIABLE CONFIG_DEFAULTS_GEN_RESULT
)

if (NOT CONFIG_DEFAULTS_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "config_defaults_gen.py failed (${CONFIG_DEFAULTS_GEN_RESULT})")
endif ()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CONFIG_EXAMPLES_DIR}/core.example.json
        ${CONFIG_EXAMPLES_DIR}/permissions.example.json
        ${CONFIG_EXAMPLES_DIR}/databases.example.json
        ${CMAKE_SOURCE_DIR}/tools/config_defaults_gen.py)

include(${CMAKE_CURRENT_LIST_DIR}/makefiles/metamod/configure_metamod.cmake)

find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_SHA
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
    )
else ()
    set(GIT_SHA "nogit")
endif ()

if (UNIX AND NOT APPLE)
    SET(source2mod_include_directories
            ${CMAKE_SOURCE_DIR}/src
            ${METAMOD_DIR}/core
            ${METAMOD_DIR}/third_party/khook/include
            ${CMAKE_SOURCE_DIR}/vendor
            ${CMAKE_SOURCE_DIR}/vendor/dynlibutils/include
            ${CMAKE_SOURCE_DIR}/vendor/dynlibutils/include/dynlibutils
            ${CMAKE_SOURCE_DIR}/vendor/openssl/include
            ${CMAKE_SOURCE_DIR}/vendor/mysql/include
            ${CMAKE_SOURCE_DIR}/vendor/spdlog/include
            # Breakpad, for the crash handler. configs.h in src/breakpad-config is
            # the autoconf output the sources expect. Only the src root goes
            # in: its subdirectories carry a module.h and a logging.h that
            # would shadow Source2Mod's and tier0's for anyone including
            # them by bare name.
            ${CMAKE_SOURCE_DIR}/src/breakpad-config/linux
            ${CMAKE_SOURCE_DIR}/vendor/breakpad/src
    )
    SET(source2mod_link_libraries
            ${CMAKE_SOURCE_DIR}/vendor/mysql/lib/libmysqlclient.a
            ${CMAKE_SOURCE_DIR}/vendor/openssl/lib/linux/libssl.a
            ${CMAKE_SOURCE_DIR}/vendor/openssl/lib/linux/libcrypto.a
            breakpad
            dynlibutils
            z
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES
            PREFIX ""
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/addons/source2mod/bin/linuxsteamrt64"
    )
elseif (WIN32)
    SET(source2mod_include_directories
            ${CMAKE_SOURCE_DIR}/src
            ${METAMOD_DIR}/core
            ${METAMOD_DIR}/third_party/khook/include
            ${CMAKE_SOURCE_DIR}/vendor
            ${CMAKE_SOURCE_DIR}/vendor/dynlibutils/include
            ${CMAKE_SOURCE_DIR}/vendor/dynlibutils/include/dynlibutils
            ${CMAKE_SOURCE_DIR}/vendor/openssl/include
            ${CMAKE_SOURCE_DIR}/vendor/mysql/include
            ${CMAKE_SOURCE_DIR}/vendor/spdlog/include
            # Breakpad, for the crash handler -- see the Linux branch for why
            # only the src root goes in.
            ${CMAKE_SOURCE_DIR}/src/breakpad-config/windows
            ${CMAKE_SOURCE_DIR}/vendor/breakpad/src
    )
    SET(source2mod_link_libraries
            ${CMAKE_SOURCE_DIR}/vendor/mysql/lib/mysqlclient.lib
            ${CMAKE_SOURCE_DIR}/vendor/openssl/lib/windows/ssl.lib
            ${CMAKE_SOURCE_DIR}/vendor/openssl/lib/windows/crypto.lib
            crypt32.lib
            # Asio's sockets.
            ws2_32.lib
            mswsock.lib
            # Crash handler: breakpad built above, DbgHelp for symbolizing
            # addons/ modules from their PDBs.
            breakpad
            dbghelp.lib
            dynlibutils
    )
    set_target_properties(${PROJECT_NAME} PROPERTIES
            PREFIX ""
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/addons/source2mod/bin/win64"
            # A DLL is a RUNTIME artifact on Windows, LIBRARY only places the import .lib.
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/addons/source2mod/bin/win64"
    )
endif ()

target_include_directories(${PROJECT_NAME} PUBLIC ${source2mod_include_directories})
target_include_directories(source2mod-sdk PUBLIC ${source2mod_include_directories})

include(${CMAKE_CURRENT_LIST_DIR}/makefiles/sourcepawn/sourcepawn.cmake)

# After source2mod_include_directories, which it builds with.
include(${CMAKE_CURRENT_LIST_DIR}/makefiles/net/net.cmake)

target_link_libraries(${PROJECT_NAME}
        source2mod-config
        source2mod-sdk
        sp-vm
        maxminddb
        sqlite3
        source2mod-net
        ${source2mod_link_libraries}
)

# breakpad_types.h insists on this being defined before <inttypes.h> is
# first seen, which with the SDK's headers is long before it gets a say.
target_compile_definitions(${PROJECT_NAME} PRIVATE __STDC_FORMAT_MACROS)

if (UNIX AND NOT APPLE)
    # Breakpad is an implementation detail of the core. Kept out of the .so's
    # export table so another crash handler loaded next to Source2Mod
    # (Accelerator, AcceleratorLocal) cannot bind to this copy of it. The same
    # for OpenSSL and the MySQL client: other plugins link their own copies
    # (InventoryManager's OpenSSL), and an exported SSL_* here could be the one
    # they end up calling.
    target_link_options(${PROJECT_NAME} PRIVATE
            -Wl,--exclude-libs=libbreakpad.a:libsp-vm.a:libsp-zlib.a:libmaxminddb.a:libsqlite3.a:libsource2mod-net.a:libssl.a:libcrypto.a:libmysqlclient.a
    )
endif ()

add_custom_command(
        TARGET ${PROJECT_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/configs ${CMAKE_BINARY_DIR}
)

# The SourcePawn include set lives in the SDK (public/scripting) and ships
# next to spcomp in addons/source2mod/scripting.
add_custom_command(
        TARGET ${PROJECT_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SOURCE2MOD_SDK_DIR}/public/scripting ${CMAKE_BINARY_DIR}/addons/source2mod/scripting/include
)
