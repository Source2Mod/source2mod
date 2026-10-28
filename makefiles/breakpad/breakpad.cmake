# Crash handler: breakpad's client, its minidump processor and libdisasm,
# built from the submodule on both platforms (the set AcceleratorCS2 builds).
# What breakpad's own autotools configure would generate (configs.h, and a
# stab.h glibc no longer ships) lives in src/breakpad-config. On Linux the
# in-process symbol dumper goes in too (ELF/DWARF to breakpad symbols, what
# lets a crash be symbolized without llvm-symbolizer or addr2line on the box);
# Windows symbolizes through DbgHelp, so nothing of breakpad's PDB tooling
# (DIA) is needed there.
set(BREAKPAD_SRC ${CMAKE_SOURCE_DIR}/vendor/breakpad/src)
set(BREAKPAD_SOURCE_FILES
        ${BREAKPAD_SRC}/processor/basic_code_modules.cc
        ${BREAKPAD_SRC}/processor/basic_source_line_resolver.cc
        ${BREAKPAD_SRC}/processor/call_stack.cc
        ${BREAKPAD_SRC}/processor/cfi_frame_info.cc
        ${BREAKPAD_SRC}/processor/convert_old_arm64_context.cc
        ${BREAKPAD_SRC}/processor/disassembler_x86.cc
        ${BREAKPAD_SRC}/processor/dump_context.cc
        ${BREAKPAD_SRC}/processor/dump_object.cc
        ${BREAKPAD_SRC}/processor/exploitability.cc
        ${BREAKPAD_SRC}/processor/exploitability_linux.cc
        ${BREAKPAD_SRC}/processor/exploitability_win.cc
        ${BREAKPAD_SRC}/processor/fast_source_line_resolver.cc
        ${BREAKPAD_SRC}/processor/logging.cc
        ${BREAKPAD_SRC}/processor/microdump.cc
        ${BREAKPAD_SRC}/processor/microdump_processor.cc
        ${BREAKPAD_SRC}/processor/minidump.cc
        ${BREAKPAD_SRC}/processor/minidump_processor.cc
        ${BREAKPAD_SRC}/processor/module_comparer.cc
        ${BREAKPAD_SRC}/processor/module_serializer.cc
        ${BREAKPAD_SRC}/processor/pathname_stripper.cc
        ${BREAKPAD_SRC}/processor/process_state.cc
        ${BREAKPAD_SRC}/processor/proc_maps_linux.cc
        ${BREAKPAD_SRC}/processor/simple_symbol_supplier.cc
        ${BREAKPAD_SRC}/processor/source_line_resolver_base.cc
        ${BREAKPAD_SRC}/processor/stack_frame_cpu.cc
        ${BREAKPAD_SRC}/processor/stack_frame_symbolizer.cc
        ${BREAKPAD_SRC}/processor/stackwalk_common.cc
        ${BREAKPAD_SRC}/processor/stackwalker.cc
        ${BREAKPAD_SRC}/processor/stackwalker_amd64.cc
        ${BREAKPAD_SRC}/processor/stackwalker_arm.cc
        ${BREAKPAD_SRC}/processor/stackwalker_arm64.cc
        ${BREAKPAD_SRC}/processor/stackwalker_address_list.cc
        ${BREAKPAD_SRC}/processor/stackwalker_mips.cc
        ${BREAKPAD_SRC}/processor/stackwalker_ppc.cc
        ${BREAKPAD_SRC}/processor/stackwalker_ppc64.cc
        ${BREAKPAD_SRC}/processor/stackwalker_riscv.cc
        ${BREAKPAD_SRC}/processor/stackwalker_riscv64.cc
        ${BREAKPAD_SRC}/processor/stackwalker_sparc.cc
        ${BREAKPAD_SRC}/processor/stackwalker_x86.cc
        ${BREAKPAD_SRC}/processor/symbolic_constants_win.cc
        ${BREAKPAD_SRC}/processor/tokenize.cc

        ${BREAKPAD_SRC}/third_party/libdisasm/ia32_implicit.c
        ${BREAKPAD_SRC}/third_party/libdisasm/ia32_insn.c
        ${BREAKPAD_SRC}/third_party/libdisasm/ia32_invariant.c
        ${BREAKPAD_SRC}/third_party/libdisasm/ia32_modrm.c
        ${BREAKPAD_SRC}/third_party/libdisasm/ia32_opcode_tables.c
        ${BREAKPAD_SRC}/third_party/libdisasm/ia32_operand.c
        ${BREAKPAD_SRC}/third_party/libdisasm/ia32_reg.c
        ${BREAKPAD_SRC}/third_party/libdisasm/ia32_settings.c
        ${BREAKPAD_SRC}/third_party/libdisasm/x86_disasm.c
        ${BREAKPAD_SRC}/third_party/libdisasm/x86_format.c
        ${BREAKPAD_SRC}/third_party/libdisasm/x86_imm.c
        ${BREAKPAD_SRC}/third_party/libdisasm/x86_insn.c
        ${BREAKPAD_SRC}/third_party/libdisasm/x86_misc.c
        ${BREAKPAD_SRC}/third_party/libdisasm/x86_operand_list.c
)
if (UNIX AND NOT APPLE)
    set(BREAKPAD_CONFIG_DIR ${CMAKE_SOURCE_DIR}/src/breakpad-config/linux)
    list(APPEND BREAKPAD_SOURCE_FILES
            ${BREAKPAD_SRC}/common/linux/scoped_pipe.cc
            ${BREAKPAD_SRC}/common/linux/scoped_tmpfile.cc
            ${BREAKPAD_SRC}/processor/disassembler_objdump.cc

            # The client.
            ${BREAKPAD_SRC}/client/linux/crash_generation/crash_generation_client.cc
            ${BREAKPAD_SRC}/client/linux/crash_generation/crash_generation_server.cc
            ${BREAKPAD_SRC}/client/linux/dump_writer_common/thread_info.cc
            ${BREAKPAD_SRC}/client/linux/dump_writer_common/ucontext_reader.cc
            ${BREAKPAD_SRC}/client/linux/handler/exception_handler.cc
            ${BREAKPAD_SRC}/client/linux/handler/minidump_descriptor.cc
            ${BREAKPAD_SRC}/client/linux/log/log.cc
            ${BREAKPAD_SRC}/client/linux/microdump_writer/microdump_writer.cc
            ${BREAKPAD_SRC}/client/linux/minidump_writer/linux_core_dumper.cc
            ${BREAKPAD_SRC}/client/linux/minidump_writer/linux_dumper.cc
            ${BREAKPAD_SRC}/client/linux/minidump_writer/linux_ptrace_dumper.cc
            ${BREAKPAD_SRC}/client/linux/minidump_writer/minidump_writer.cc
            ${BREAKPAD_SRC}/client/linux/minidump_writer/pe_file.cc
            ${BREAKPAD_SRC}/client/minidump_file_writer.cc
            ${BREAKPAD_SRC}/common/convert_UTF.cc
            ${BREAKPAD_SRC}/common/md5.cc
            ${BREAKPAD_SRC}/common/string_conversion.cc
            ${BREAKPAD_SRC}/common/linux/elf_core_dump.cc
            ${BREAKPAD_SRC}/common/linux/elfutils.cc
            ${BREAKPAD_SRC}/common/linux/file_id.cc
            ${BREAKPAD_SRC}/common/linux/guid_creator.cc
            ${BREAKPAD_SRC}/common/linux/linux_libc_support.cc
            ${BREAKPAD_SRC}/common/linux/memory_mapped_file.cc
            ${BREAKPAD_SRC}/common/linux/safe_readlink.cc

            # The symbol dumper.
            ${BREAKPAD_SRC}/common/dwarf_cfi_to_module.cc
            ${BREAKPAD_SRC}/common/dwarf_cu_to_module.cc
            ${BREAKPAD_SRC}/common/dwarf_line_to_module.cc
            ${BREAKPAD_SRC}/common/dwarf_range_list_handler.cc
            ${BREAKPAD_SRC}/common/language.cc
            ${BREAKPAD_SRC}/common/module.cc
            ${BREAKPAD_SRC}/common/path_helper.cc
            ${BREAKPAD_SRC}/common/stabs_reader.cc
            ${BREAKPAD_SRC}/common/stabs_to_module.cc
            ${BREAKPAD_SRC}/common/dwarf/bytereader.cc
            ${BREAKPAD_SRC}/common/dwarf/dwarf2diehandler.cc
            ${BREAKPAD_SRC}/common/dwarf/dwarf2reader.cc
            ${BREAKPAD_SRC}/common/dwarf/elf_reader.cc
            ${BREAKPAD_SRC}/common/linux/crc32.cc
            ${BREAKPAD_SRC}/common/linux/dump_symbols.cc
            ${BREAKPAD_SRC}/common/linux/elf_symbols_to_module.cc
    )
elseif (WIN32)
    set(BREAKPAD_CONFIG_DIR ${CMAKE_SOURCE_DIR}/src/breakpad-config/windows)
    list(APPEND BREAKPAD_SOURCE_FILES
            ${BREAKPAD_SRC}/client/windows/crash_generation/client_info.cc
            ${BREAKPAD_SRC}/client/windows/crash_generation/crash_generation_client.cc
            ${BREAKPAD_SRC}/client/windows/crash_generation/crash_generation_server.cc
            ${BREAKPAD_SRC}/client/windows/crash_generation/minidump_generator.cc
            ${BREAKPAD_SRC}/client/windows/handler/exception_handler.cc
            ${BREAKPAD_SRC}/common/windows/guid_string.cc
            ${BREAKPAD_SRC}/common/windows/string_utils.cc
            ${BREAKPAD_SRC}/common/convert_UTF.cc
            ${BREAKPAD_SRC}/common/string_conversion.cc
    )
endif ()

add_library(breakpad STATIC ${BREAKPAD_SOURCE_FILES})
target_include_directories(breakpad PRIVATE
        ${BREAKPAD_CONFIG_DIR}
        ${BREAKPAD_SRC}
)
if (UNIX AND NOT APPLE)
    set_target_properties(breakpad PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # The core's std::string ABI (s2sdk's): crashhandler.cpp passes strings in.
    target_compile_definitions(breakpad PRIVATE HAVE_CONFIG_H _GNU_SOURCE _GLIBCXX_USE_CXX11_ABI=0)
    target_compile_options(breakpad PRIVATE -w)
    # configs.h says HAVE_A_OUT_H, and <a.out.h>'s N_* macros then collide
    # with the enum src/breakpad-config's stab.h builds from stab.def. The
    # stabs reader went without the config before, inside the core; it still
    # does.
    set_source_files_properties(${BREAKPAD_SRC}/common/stabs_reader.cc
            PROPERTIES COMPILE_OPTIONS "-UHAVE_CONFIG_H"
    )
elseif (WIN32)
    target_compile_definitions(breakpad PRIVATE
            HAVE_CONFIG_H
            # The client is written against the wide Win32 API.
            UNICODE
            _UNICODE
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_DEPRECATE
            # minidump_generator.cc still uses stdext::checked_array_iterator,
            # deprecated since VS 2022 17.8 and silenced here on toolsets
            # that still ship it.
            _SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING
    )
    target_compile_options(breakpad PRIVATE /utf-8 /EHsc /wd4267 /wd4244 /wd4996)
    # VS 2026 (MSVC 14.50+) removed stdext::checked_array_iterator from the
    # STL entirely. Force-include a drop-in replacement into the one file
    # that uses it instead of patching the submodule; the header is a no-op
    # on older toolsets.
    set_source_files_properties(
            ${BREAKPAD_SRC}/client/windows/crash_generation/minidump_generator.cc
            PROPERTIES COMPILE_OPTIONS
            "/FI${CMAKE_SOURCE_DIR}/src/breakpad-config/windows/stdext_compat.h"
    )
endif ()
