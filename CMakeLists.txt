cmake_minimum_required(VERSION 3.20)
project(anastasia_engine C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti -O3 -Wall -Wextra")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -nostdlib -nodefaultlibs -Wl,-e,_start")
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
    src/backend/ana_lowerer.cpp
    src/optimizer/ana_ssa.cpp
    src/optimizer/pgo_profiler.cpp
    tests/leetcode_suite.cpp
    tests/test_suite.cpp
    examples/example_runner.cpp
    src/main.cpp
)

add_executable(anastasia_engine ${SOURCES})
target_compile_options(anastasia_engine PRIVATE -ffreestanding)

set(BENCH_SOURCES
    src/sys/sys_raw.cpp
    src/sys/sys_entry.cpp
    src/sys/cpu_features.cpp
    src/sys/object_heap.cpp
    src/sys/tlab_provider.cpp
    src/sys/gc_collector.cpp
    src/sys/io_ring.cpp
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
    src/backend/ana_lowerer.cpp
    src/optimizer/ana_ssa.cpp
    src/optimizer/pgo_profiler.cpp
    benchmark/bench_suite.cpp
    benchmark/bench_main.cpp
)

add_executable(anastasia_benchmark ${BENCH_SOURCES})
target_compile_options(anastasia_benchmark PRIVATE -ffreestanding)
