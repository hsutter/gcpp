cmake_minimum_required(VERSION 3.1)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED on)

include_directories(SYSTEM submodules/gsl/include)

add_executable(test_basic test.cpp)
add_executable(test_graph test_graph.cpp)

enable_testing()

add_test(test_basic ${CMAKE_BINARY_DIR}/test_basic -s)

# FIXME: Fix graph tests which fail with an assertion error on macOS.
# add_test(test_graph ${CMAKE_BINARY_DIR}/test_graph -s)
