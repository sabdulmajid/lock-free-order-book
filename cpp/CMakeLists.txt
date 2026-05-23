cmake_minimum_required(VERSION 3.20)
project(order_book_cpp)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

option(LFOB_BUILD_BENCHMARKS "Build Google Benchmark executables" ON)
option(LFOB_BUILD_TESTS "Build C++ stress tests" ON)
option(LFOB_ENABLE_NATIVE_ARCH "Enable -march=native for host-specific local benchmark builds" OFF)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-std=gnu++2c" LFOB_HAS_GNUXX2C)
check_cxx_compiler_flag("-std=gnu++26" LFOB_HAS_GNUXX26)
check_cxx_compiler_flag("-fexperimental-library" LFOB_HAS_EXPERIMENTAL_LIBRARY)

set(LFOB_CXX26_EXPERIMENTAL 0)
set(LFOB_EXPERIMENTAL_STANDARD_FLAG "")
if (LFOB_HAS_GNUXX2C)
  set(LFOB_CXX26_EXPERIMENTAL 1)
  set(LFOB_EXPERIMENTAL_STANDARD_FLAG "-std=gnu++2c")
elseif (LFOB_HAS_GNUXX26)
  set(LFOB_CXX26_EXPERIMENTAL 1)
  set(LFOB_EXPERIMENTAL_STANDARD_FLAG "-std=gnu++26")
else()
  set(CMAKE_CXX_STANDARD 23)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS ON)
endif()

function(configure_low_latency_target target_name)
  if (NOT LFOB_CXX26_EXPERIMENTAL)
    target_compile_features(${target_name} PUBLIC cxx_std_23)
  endif()

  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target_name} PRIVATE -O3)
    if (LFOB_ENABLE_NATIVE_ARCH)
      target_compile_options(${target_name} PRIVATE -march=native)
    endif()
    if (LFOB_CXX26_EXPERIMENTAL)
      target_compile_options(${target_name} PRIVATE ${LFOB_EXPERIMENTAL_STANDARD_FLAG})
    endif()
    if (LFOB_HAS_EXPERIMENTAL_LIBRARY)
      target_compile_options(${target_name} PRIVATE -fexperimental-library)
    endif()
  endif()
  target_compile_definitions(${target_name} PUBLIC LFOB_CXX26_EXPERIMENTAL=${LFOB_CXX26_EXPERIMENTAL})
endfunction()

# --- Library ---
add_library(order_book_lib src/order.cpp src/order_book.cpp)
target_include_directories(order_book_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
configure_low_latency_target(order_book_lib)

# --- Main Executable ---
add_executable(order_book_cpp src/main.cpp)
target_link_libraries(order_book_cpp PRIVATE order_book_lib)
target_include_directories(order_book_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
configure_low_latency_target(order_book_cpp)

find_package(Threads REQUIRED)

if (LFOB_BUILD_TESTS)
  enable_testing()
  add_executable(low_latency_stress tests/low_latency_stress.cpp)
  configure_low_latency_target(low_latency_stress)
  target_link_libraries(low_latency_stress PRIVATE Threads::Threads)
  target_include_directories(low_latency_stress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  add_test(NAME low_latency_stress COMMAND low_latency_stress)
endif()

if (LFOB_BUILD_BENCHMARKS)
  include(FetchContent)
  FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
  )
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark testing" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable benchmark install" FORCE)
  FetchContent_MakeAvailable(googlebenchmark)

  add_executable(order_book_benches benches/order_book_benches.cpp)
  configure_low_latency_target(order_book_benches)
  target_link_libraries(order_book_benches PRIVATE
    order_book_lib
    benchmark::benchmark
    benchmark::benchmark_main
    Threads::Threads
  )
  target_include_directories(order_book_benches PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

  add_executable(concurrent_queue_benches benches/concurrent_queue_benches.cpp)
  configure_low_latency_target(concurrent_queue_benches)
  target_link_libraries(concurrent_queue_benches PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    Threads::Threads
  )
  target_include_directories(concurrent_queue_benches PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

  add_executable(concurrent_order_book_benches benches/concurrent_order_book_benches.cpp)
  configure_low_latency_target(concurrent_order_book_benches)
  target_link_libraries(concurrent_order_book_benches PRIVATE
    order_book_lib
    benchmark::benchmark
    benchmark::benchmark_main
    Threads::Threads
  )
  target_include_directories(concurrent_order_book_benches PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()
