cmake_minimum_required(VERSION 3.20)
project(PA250Sim LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(pa250sim STATIC
  src/turn.cpp src/battle.cpp src/diplomacy.cpp src/serialize.cpp src/controller.cpp)
target_include_directories(pa250sim PUBLIC include third_party PRIVATE src)
target_compile_options(pa250sim PRIVATE -Wall -Wextra -Wno-unused-parameter)

enable_testing()
add_executable(pa250sim_tests tests/test_main.cpp)
target_link_libraries(pa250sim_tests PRIVATE pa250sim)
target_compile_definitions(pa250sim_tests PRIVATE
  PA250_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  PA250_VECTOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../protocol/test_vectors/clauses")
add_test(NAME pa250sim COMMAND pa250sim_tests)
