cmake_minimum_required(VERSION 3.20)
project(anastasia_engine C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/O2 /W4 /GR- /EHa-)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti -O3 -Wall -Wextra")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64" OR CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "ARM64")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mno-outline-atomics")
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -nostdlib -nodefaultlibs -Wl,-e,_start -lgcc")
    endif()
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(SOURCES
    src/sys/sys_raw.cpp
    src/sys/sys_entry.cpp
    src/sys/cpu_features.cpp
    src/sys/object_heap.cpp
    src/sys/tlab_provider.cpp
    src/sys/gc_collector.cpp
    src/sys/io_ring.cpp
    src/sys/ana_trap_handler.cpp
    src/frontend/ana_lexer.cpp
    src/frontend/ana_parser.cpp
    src/backend/ana_encoder.cpp
    src/backend/ana_regalloc.cpp
    src/backend/elf_emitter.cpp
    src/backend/pe_emitter.cpp
    src/backend/gdb_jit.cpp
    src/backend/dwarf_emitter.cpp
    src/backend/inline_cache.cpp
    src/backend/pic_dispatcher.cpp
    src/backend/exception_unwinder.cpp
    src/backend/osr_engine.cpp
    src/backend/jit_string_pool.cpp
    src/backend/host_interop.cpp
    src/backend/x86_64_backend.cpp
    src/backend/aarch64_backend.cpp
    src/backend/armv7_backend.cpp
    src/backend/rv64_backend.cpp
    src/backend/ana_lowerer.cpp
    src/optimizer/ana_ssa.cpp
    src/optimizer/pgo_profiler.cpp
    src/optimizer/sys_coalescer.cpp
    src/debugger/ana_debugger.cpp
    tests/leetcode_suite.cpp
    tests/codeforces_suite.cpp
    tests/hardcore_suite.cpp
    tests/reliability_suite.cpp
    tests/test_suite.cpp
    examples/example_runner.cpp
    src/main.cpp
)

add_executable(anastasia_engine ${SOURCES})

set(BENCH_SOURCES
    src/sys/sys_raw.cpp
    src/sys/sys_entry.cpp
    src/sys/cpu_features.cpp
    src/sys/object_heap.cpp
    src/sys/tlab_provider.cpp
    src/sys/gc_collector.cpp
    src/sys/io_ring.cpp
    src/sys/ana_trap_handler.cpp
    src/frontend/ana_lexer.cpp
    src/frontend/ana_parser.cpp
    src/backend/ana_encoder.cpp
    src/backend/ana_regalloc.cpp
    src/backend/elf_emitter.cpp
    src/backend/gdb_jit.cpp
    src/backend/dwarf_emitter.cpp
    src/backend/inline_cache.cpp
    src/backend/pic_dispatcher.cpp
    src/backend/exception_unwinder.cpp
    src/backend/osr_engine.cpp
    src/backend/jit_string_pool.cpp
    src/backend/host_interop.cpp
    src/backend/x86_64_backend.cpp
    src/backend/aarch64_backend.cpp
    src/backend/armv7_backend.cpp
    src/backend/rv64_backend.cpp
    src/backend/ana_lowerer.cpp
    src/optimizer/ana_ssa.cpp
    src/optimizer/pgo_profiler.cpp
    src/optimizer/sys_coalescer.cpp
    benchmark/bench_suite.cpp
    benchmark/bench_main.cpp
)

add_executable(anastasia_benchmark ${BENCH_SOURCES})

if(NOT MSVC)
    target_compile_options(anastasia_engine PRIVATE -ffreestanding)
    target_compile_options(anastasia_benchmark PRIVATE -ffreestanding)
    target_link_libraries(anastasia_engine PRIVATE gcc)
    target_link_libraries(anastasia_benchmark PRIVATE gcc)
endif()
