cmake_minimum_required(VERSION 3.5)
find_package(LLVM REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

# AddLLVM needs these
set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib)

include(HandleLLVMOptions) # important: matches compiler flags to LLVM/Clang build
include(AddLLVM)
#add_library(LLVMContech MODULE Contech.cpp)

add_llvm_loadable_module( LLVMContech
  Contech.cpp
  )

link_directories(${CMAKE_SOURCE_DIR}/loop)
add_library(loop SHARED IMPORTED)
set_target_properties(loop PROPERTIES
  IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/loop/loop.so"
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/loop"
)
target_link_libraries(LLVMContech loop)
