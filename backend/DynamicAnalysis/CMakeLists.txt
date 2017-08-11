cmake_minimum_required(VERSION 2.8.12.2)

find_package(LLVM REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -g")
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L../../common/taskLib/ -ltaskLib -Wl,-rpath=../../common/taskLib/")

# AddLLVM needs these
set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib)

include(HandleLLVMOptions) # important: matches compiler flags to LLVM/Clang build
include(AddLLVM)

link_directories("../../common/taskLib/")
find_library(TL NAMES libTask.a PATHS "../../common/taskLib/")

#add_library(LLVMContech MODULE Contech.cpp)

add_llvm_loadable_module( LLVMDynAnalysis
  DynamicAnalysis.cpp
  Execution.cpp
  Final.cpp
  Init.cpp
  Print.cpp
  TBV.cpp
  )
  
  
#add_library(taskLib SHARED IMPORTED)
#set(CMAKE_INSTALL_RPATH "../../common/taskLib/")
target_link_libraries(LLVMDynAnalysis Task)