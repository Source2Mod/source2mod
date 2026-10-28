# GeoIP: libmaxminddb as a static library, nothing else of it (tests, the
# mmdblookup binary, install rules).
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(MAXMINDDB_BUILD_BINARIES OFF CACHE BOOL "" FORCE)
set(MAXMINDDB_INSTALL OFF CACHE BOOL "" FORCE)
set(MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/vendor/maxminddb ${CMAKE_BINARY_DIR}/vendor/maxminddb EXCLUDE_FROM_ALL)
