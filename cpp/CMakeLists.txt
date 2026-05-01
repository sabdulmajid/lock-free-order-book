cmake_minimum_required(VERSION 3.10)
project(order_book_cpp)

# C++ Standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

# --- Library ---
add_library(order_book_lib src/order.cpp src/order_book.cpp)
target_include_directories(order_book_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- Main Executable ---
add_executable(order_book_cpp src/main.cpp)
target_link_libraries(order_book_cpp PRIVATE order_book_lib)
target_include_directories(order_book_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

enable_testing()

include(FetchContent)
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark testing" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable benchmark install" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# --- Benchmarks ---
find_package(Threads REQUIRED)

add_executable(order_book_benches benches/order_book_benches.cpp)
target_link_libraries(order_book_benches PRIVATE
  order_book_lib
  benchmark::benchmark
  benchmark::benchmark_main
  Threads::Threads
)
target_include_directories(order_book_benches PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  
# --- Concurrent Benchmarks ---
add_executable(concurrent_queue_benches benches/concurrent_queue_benches.cpp)
target_link_libraries(concurrent_queue_benches PRIVATE
  benchmark::benchmark
  benchmark::benchmark_main
  Threads::Threads
)
target_include_directories(concurrent_queue_benches PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(concurrent_order_book_benches benches/concurrent_order_book_benches.cpp)
target_link_libraries(concurrent_order_book_benches PRIVATE
  order_book_lib
  benchmark::benchmark
  benchmark::benchmark_main
  Threads::Threads
)
target_include_directories(concurrent_order_book_benches PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

