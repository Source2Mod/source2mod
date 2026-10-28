# The core's own sockets (src/core/net.cpp, net_*.cpp), the only sources that
# include Boost: Asio and Beast, header-only, out of vendor/boost (the
# SlynxCZ/boost submodule, the same as InventoryManager's).
# They build into source2mod-net, which the core links with --exclude-libs like
# breakpad: visibility flags alone still leave Boost's BOOST_SYMBOL_VISIBLE
# vtables and typeinfo in the export table. The SDK and the plugins never see
# these headers at all. Needs source2mod_include_directories.

file(GLOB NET_SOURCE_FILES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/core/net.cpp
        ${CMAKE_SOURCE_DIR}/src/core/net_*.cpp
)

# Before OpenSSL on the core's link line: it is what pulls OpenSSL's TLS
# symbols in.
add_library(source2mod-net STATIC ${NET_SOURCE_FILES})
set_target_properties(source2mod-net PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(source2mod-net PRIVATE ${source2mod_include_directories})
target_link_libraries(source2mod-net PRIVATE source2mod-config)
target_compile_definitions(source2mod-net PRIVATE
        # Asio's deadline_timer and regex read_until, and the Boost.DateTime and
        # Boost.Regex they drag in: steady_timer and plain read_until do.
        BOOST_ASIO_DISABLE_BOOST_DATE_TIME
        BOOST_ASIO_DISABLE_BOOST_REGEX
        # The core's std::string ABI (s2sdk's): net.h hands strings across.
        $<$<PLATFORM_ID:Linux>:_GLIBCXX_USE_CXX11_ABI=0>
        $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>
)
