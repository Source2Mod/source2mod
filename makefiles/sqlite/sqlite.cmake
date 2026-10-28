# SQLite, the amalgamation (SourceMod's copy, 3.47.0): client preferences, and
# the SQL layer's SQLite driver.
add_library(sqlite3 STATIC ${CMAKE_SOURCE_DIR}/vendor/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_SOURCE_DIR}/vendor/sqlite)
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DEFAULT_FOREIGN_KEYS=1)
