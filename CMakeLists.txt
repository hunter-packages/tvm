cmake_minimum_required(VERSION 3.2)

include("cmake/HunterGate.cmake")
HunterGate(
    URL "https://github.com/ruslo/hunter/archive/v0.23.142.tar.gz"
    SHA1 "1d841ca74150c92054896a7f6d88a123dd9e615d"
)

project(tvm VERSION 0.5)

# Utility functions
include(cmake/util/Util.cmake)
include(cmake/util/FindCUDA.cmake)
include(cmake/util/FindVulkan.cmake)
include(cmake/util/FindLLVM.cmake)
include(cmake/util/FindROCM.cmake)

if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/config.cmake)
  include(${CMAKE_CURRENT_BINARY_DIR}/config.cmake)
else()
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config.cmake)
    include(${CMAKE_CURRENT_SOURCE_DIR}/config.cmake)
  endif()
endif()

# NOTE: do not modify this file to change option values.
# You can create a config.cmake at build folder
# and add set(OPTION VALUE) to override these build options.
# Alernatively, use cmake -DOPTION=VALUE through command-line.
tvm_option(USE_CUDA "Build with CUDA" OFF)
tvm_option(USE_OPENCL "Build with OpenCL" OFF)
tvm_option(USE_VULKAN "Build with Vulkan" OFF)
tvm_option(USE_OPENGL "Build with OpenGL" OFF)
tvm_option(USE_METAL "Build with Metal" OFF)
tvm_option(USE_ROCM "Build with ROCM" OFF)
tvm_option(ROCM_PATH "The path to rocm" /opt/rocm)
tvm_option(USE_RPC "Build with RPC" ON)
tvm_option(USE_LLVM "Build with LLVM, can be set to specific llvm-config path" OFF)
tvm_option(USE_STACKVM_RUNTIME "Include stackvm into the runtime" OFF)
tvm_option(USE_GRAPH_RUNTIME "Build with tiny graph runtime" ON)
tvm_option(USE_GRAPH_RUNTIME_DEBUG "Build with tiny graph runtime debug mode" OFF)
tvm_option(USE_RELAY_DEBUG "Building Relay in debug mode..." OFF)
tvm_option(USE_SGX "Build with SGX" OFF)
tvm_option(USE_RTTI "Build with RTTI" ON)
tvm_option(USE_MSVC_MT "Build with MT" OFF)
tvm_option(INSTALL_DEV "Install compiler infrastructure" OFF)
tvm_option(INSTALL_NNVM_SOURCES "Install NNVM sources" OFF)
tvm_option(TVM_BUILD_RUNTIME_ONLY "Only build tvm_runtime library" OFF)

# Contrib library options
tvm_option(USE_BLAS "The blas library to be linked" none)
tvm_option(USE_MKL_PATH "MKL root path when use MKL blas" none)
tvm_option(USE_CUDNN "Build with cuDNN" OFF)
tvm_option(USE_CUBLAS "Build with cuBLAS" OFF)
tvm_option(USE_MIOPEN "Build with ROCM:MIOpen" OFF)
tvm_option(USE_ROCBLAS "Build with ROCM:RoCBLAS" OFF)
tvm_option(USE_SORT "Build with sort support" OFF)
tvm_option(USE_NNPACK "Build with nnpack support" OFF)
tvm_option(USE_RANDOM "Build with random support" OFF)
tvm_option(USE_ANTLR "Build with ANTLR for Relay parsing" OFF)

# include directories
include_directories("include")

if(NOT HUNTER_ENABLED)
include_directories("3rdparty/dlpack/include")
include_directories("3rdparty/dmlc-core/include")
include_directories("3rdparty/rang/include")
endif()
include_directories("3rdparty/compiler-rt")

# initial variables
set(TVM_LINKER_LIBS "")
set(TVM_RUNTIME_LINKER_LIBS ${CMAKE_DL_LIBS})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Generic compilation options
if(MSVC)
  add_definitions(-DWIN32_LEAN_AND_MEAN)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  add_definitions(-D_SCL_SECURE_NO_WARNINGS)
  add_definitions(-D_ENABLE_EXTENDED_ALIGNED_STORAGE)
  add_definitions(-DHalide_SHARED)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /bigobj")
  if(USE_MSVC_MT)
    foreach(flag_var
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
      if(${flag_var} MATCHES "/MD")
        string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
      endif(${flag_var} MATCHES "/MD")
    endforeach(flag_var)
  endif()
else(MSVC)
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("-std=c++11"    SUPPORT_CXX11)
  if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    message("Build in Debug mode")
    set(CMAKE_C_FLAGS "-O0 -g -Wall -fPIC ${CMAKE_C_FLAGS} -rdynamic")
    set(CMAKE_CXX_FLAGS "-O0 -g -Wall -fPIC -std=c++11 ${CMAKE_CXX_FLAGS} -rdynamic")
  else()
    set(CMAKE_C_FLAGS "-O2 -Wall -fPIC -fvisibility=hidden ${CMAKE_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "-O2 -Wall -fPIC -fvisibility=hidden -std=c++11 ${CMAKE_CXX_FLAGS}")
  endif ()
  if (CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND
      CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0)
    set(CMAKE_CXX_FLAGS "-faligned-new ${CMAKE_CXX_FLAGS}")
  endif()
endif(MSVC)

# add source group
FILE(GLOB_RECURSE GROUP_SOURCE "src/*.cc" "3rdparty/HalideIR/src/*.cpp" "nnvm/src/*.cc")
FILE(GLOB_RECURSE GROUP_INCLUDE "src/*.h" "include/*.h" "3rdparty/HalideIR/src/*.h"
                                "nnvm/src/*.h" "nnvm/include/*.h")
assign_source_group("Source" ${GROUP_SOURCE})
assign_source_group("Include" ${GROUP_INCLUDE})

# Source file lists
file(GLOB COMPILER_SRCS
    src/api/*.cc
    src/arithmetic/*.cc
    src/autotvm/*.cc
    src/codegen/*.cc
    src/lang/*.cc
    src/pass/*.cc
    src/op/*.cc
    src/schedule/*.cc
    )

file(GLOB_RECURSE RELAY_SRCS
    src/relay/*.cc
    )
list(APPEND COMPILER_SRCS ${RELAY_SRCS})

file(GLOB DATATYPE_SRCS src/codegen/datatype/*.cc)
list(APPEND COMPILER_SRCS ${DATATYPE_SRCS})

if(NOT MSVC)
  file(GLOB COMPILER_VERILOG_SRCS src/codegen/verilog/*.cc)
  list(APPEND COMPILER_SRCS ${COMPILER_VERILOG_SRCS})
endif()

file(GLOB_RECURSE NNVM_COMPILER_SRCS
    nnvm/src/c_api/*.cc
    nnvm/src/core/*.cc
    nnvm/src/pass/*.cc
    nnvm/src/compiler/*.cc
    nnvm/src/top/*.cc
    )

if(INSTALL_NNVM_SOURCES)
  file(GLOB_RECURSE x nnvm/src/c_api/*.cc nnvm/src/c_api/*.h)
  install(FILES ${x} DESTINATION src/tvm/nnvm/c_api)

  file(GLOB_RECURSE x nnvm/src/core/*.cc)
  install(FILES ${x} DESTINATION src/tvm/nnvm/core)

  file(GLOB_RECURSE x nnvm/src/pass/*.cc nnvm/src/pass/*.h)
  install(FILES ${x} DESTINATION src/tvm/nnvm/pass)

  file(GLOB_RECURSE x nnvm/src/compiler/*.cc nnvm/src/compiler/*.h)
  install(FILES ${x} DESTINATION src/tvm/nnvm/compiler)

  file(GLOB_RECURSE x nnvm/src/top/*.cc)
  install(FILES ${x} DESTINATION src/tvm/nnvm/top)
endif()

file(GLOB TOPI_SRCS
    topi/src/*.cc
)
if(NOT HUNTER_ENABLED)
file(GLOB_RECURSE HALIDEIR_SRCS 3rdparty/HalideIR/src/*.cpp)
list(APPEND COMPILER_SRCS ${HALIDEIR_SRCS})
endif()
file(GLOB RUNTIME_SRCS
  src/runtime/*.cc
  src/runtime/vm/*.cc
)

# Package runtime rules
if(NOT USE_RTTI)
  add_definitions(-DDMLC_ENABLE_RTTI=0)
endif()

list(APPEND RUNTIME_SRCS 3rdparty/bfloat16/bfloat16.cc)

if(USE_RPC)
  message(STATUS "Build with RPC support...")
  file(GLOB RUNTIME_RPC_SRCS src/runtime/rpc/*.cc)
  list(APPEND RUNTIME_SRCS ${RUNTIME_RPC_SRCS})
endif(USE_RPC)

file(GLOB STACKVM_RUNTIME_SRCS src/runtime/stackvm/*.cc)
file(GLOB STACKVM_CODEGEN_SRCS src/codegen/stackvm/*.cc)
list(APPEND COMPILER_SRCS ${STACKVM_CODEGEN_SRCS})
if(USE_STACKVM_RUNTIME)
  message(STATUS "Build with stackvm support in runtime...")
  list(APPEND RUNTIME_SRCS ${STACKVM_RUNTIME_SRCS})
else()
  list(APPEND COMPILER_SRCS ${STACKVM_RUNTIME_SRCS})
endif(USE_STACKVM_RUNTIME)

if(USE_GRAPH_RUNTIME)
  message(STATUS "Build with Graph runtime support...")
  file(GLOB RUNTIME_GRAPH_SRCS src/runtime/graph/*.cc)
  list(APPEND RUNTIME_SRCS ${RUNTIME_GRAPH_SRCS})

  if(USE_GRAPH_RUNTIME_DEBUG)
    message(STATUS "Build with Graph runtime debug support...")
    file(GLOB RUNTIME_GRAPH_DEBUG_SRCS src/runtime/graph/debug/*.cc)
    list(APPEND RUNTIME_SRCS ${RUNTIME_GRAPH_DEBUG_SRCS})
    set_source_files_properties(${RUNTIME_GRAPH_SRCS}
      PROPERTIES COMPILE_DEFINITIONS "TVM_GRAPH_RUNTIME_DEBUG")
  endif(USE_GRAPH_RUNTIME_DEBUG)
endif(USE_GRAPH_RUNTIME)

# Module rules
include(cmake/modules/VTA.cmake)
include(cmake/modules/CUDA.cmake)
include(cmake/modules/OpenCL.cmake)
include(cmake/modules/OpenGL.cmake)
include(cmake/modules/Vulkan.cmake)
include(cmake/modules/Metal.cmake)
include(cmake/modules/ROCM.cmake)
include(cmake/modules/SGX.cmake)
include(cmake/modules/LLVM.cmake)
include(cmake/modules/ANTLR.cmake)
include(cmake/modules/contrib/BLAS.cmake)
include(cmake/modules/contrib/Random.cmake)
include(cmake/modules/contrib/Sort.cmake)
include(cmake/modules/contrib/NNPack.cmake)
include(cmake/modules/contrib/HybridDump.cmake)

hunter_add_package(dlpack)
find_package(dlpack CONFIG REQUIRED)

if(NOT TVM_BUILD_RUNTIME_ONLY)
add_library(tvm SHARED ${COMPILER_SRCS} ${RUNTIME_SRCS})
add_library(tvm_topi SHARED ${TOPI_SRCS})
endif()
add_library(tvm_runtime SHARED ${RUNTIME_SRCS})

if(USE_RELAY_DEBUG)
  message(STATUS "Building Relay in debug mode...")
  set_target_properties(tvm PROPERTIES COMPILE_DEFINITIONS "USE_RELAY_DEBUG")
  set_target_properties(tvm PROPERTIES COMPILE_DEFINITIONS "NDEBUG")
endif(USE_RELAY_DEBUG)

if(NOT USE_SGX STREQUAL "OFF")
  add_dependencies(tvm sgx_edl)
  add_dependencies(tvm_runtime sgx_edl tvm_t)
  install(TARGETS tvm_t ARCHIVE DESTINATION lib${LIB_SUFFIX})
endif()
if(NOT TVM_BUILD_RUNTIME_ONLY)
add_library(nnvm_compiler SHARED ${NNVM_COMPILER_SRCS})
endif()

if(NOT TVM_BUILD_RUNTIME_ONLY)
target_link_libraries(tvm PUBLIC ${TVM_LINKER_LIBS} ${TVM_RUNTIME_LINKER_LIBS})
target_link_libraries(tvm_topi PUBLIC tvm ${TVM_LINKER_LIBS} ${TVM_RUNTIME_LINKER_LIBS})
endif()
target_link_libraries(tvm_runtime PUBLIC ${TVM_RUNTIME_LINKER_LIBS})
if(NOT TVM_BUILD_RUNTIME_ONLY)
target_link_libraries(nnvm_compiler PUBLIC tvm)

find_package(Threads REQUIRED)
target_link_libraries(tvm PUBLIC Threads::Threads)

target_link_libraries(tvm PUBLIC ${CMAKE_DL_LIBS})

# Related headers
target_include_directories(
  tvm
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/topi/include>")

if(NOT HUNTER_ENABLED)
target_include_directories(
  tvm
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/3rdparty/HalideIR/src>")
endif()

target_include_directories(
  tvm_topi
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/topi/include>")
target_include_directories(
  nnvm_compiler
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/nnvm/include>"
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/topi/include>")

hunter_add_package(HalideIR)
find_package(HalideIR CONFIG REQUIRED)
target_link_libraries(tvm PUBLIC HalideIR::HalideIR)

target_link_libraries(tvm PUBLIC dlpack::dlpack)

hunter_add_package(rang)
find_package(rang CONFIG REQUIRED)
target_link_libraries(tvm PUBLIC rang::rang)
endif()

hunter_add_package(dmlc-core)
find_package(dmlc CONFIG REQUIRED)
target_link_libraries(tvm_runtime PUBLIC dmlc::dmlc dlpack::dlpack)

# Tests
set(TEST_EXECS "")
file(GLOB TEST_SRCS tests/cpp/*.cc)
find_library(GTEST_LIB gtest "$ENV{GTEST_LIB}")

if(GTEST_LIB)
  foreach(__srcpath ${TEST_SRCS})
    get_filename_component(__srcname ${__srcpath} NAME)
    string(REPLACE ".cc" "" __execname ${__srcname})
    add_executable(${__execname} ${__srcpath})
    list(APPEND TEST_EXECS ${__execname})
    target_link_libraries(${__execname}
      tvm ${GTEST_LIB} pthread dl)
    set_target_properties(${__execname} PROPERTIES EXCLUDE_FROM_ALL 1)
    set_target_properties(${__execname} PROPERTIES EXCLUDE_FROM_DEFAULT_BUILD 1)
  endforeach()
  add_custom_target(cpptest DEPENDS ${TEST_EXECS})
endif()

if(NOT TVM_BUILD_RUNTIME_ONLY)
# Custom targets
add_custom_target(runtime DEPENDS tvm_runtime)
endif()

### Installation (https://github.com/forexample/package-example) {

# Introduce variables:
# * CMAKE_INSTALL_LIBDIR
# * CMAKE_INSTALL_BINDIR
# * CMAKE_INSTALL_INCLUDEDIR
include(GNUInstallDirs)

set(config_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(version_config "${generated_dir}/${PROJECT_NAME}ConfigVersion.cmake")
set(project_config "${generated_dir}/${PROJECT_NAME}Config.cmake")
set(TARGETS_EXPORT_NAME "${PROJECT_NAME}Targets")
set(namespace "${PROJECT_NAME}::")

include(CMakePackageConfigHelpers)

# Use:
# * PROJECT_VERSION
write_basic_package_version_file(
    "${version_config}" COMPATIBILITY SameMajorVersion
)

# Use:
# * TARGETS_EXPORT_NAME
# * PROJECT_NAME
configure_package_config_file(
    "cmake/template/Config.cmake.in"
    "${project_config}"
    INSTALL_DESTINATION "${config_install_dir}"
)

install(
    FILES "${project_config}" "${version_config}"
    DESTINATION "${config_install_dir}"
)

install(
    EXPORT "${TARGETS_EXPORT_NAME}"
    NAMESPACE "${namespace}"
    DESTINATION "${config_install_dir}"
)

### }

# Installation rulse
install(TARGETS tvm_runtime EXPORT "${TARGETS_EXPORT_NAME}" DESTINATION lib${LIB_SUFFIX})
if(NOT TVM_BUILD_RUNTIME_ONLY)
install(TARGETS nnvm_compiler EXPORT "${TARGETS_EXPORT_NAME}" DESTINATION lib${LIB_SUFFIX})
endif()

if(TARGET vta)
  install(TARGETS vta EXPORT "${TARGETS_EXPORT_NAME}" DESTINATION lib${LIB_SUFFIX})
endif()

if (INSTALL_DEV)
  if(NOT TVM_BUILD_RUNTIME_ONLY)
  install(TARGETS tvm tvm_topi EXPORT "${TARGETS_EXPORT_NAME}" DESTINATION lib${LIB_SUFFIX})
  endif()
  install(
    DIRECTORY "include/." DESTINATION "include"
    FILES_MATCHING
    PATTERN "*.h"
  )
  install(
    DIRECTORY "topi/include/." DESTINATION "include"
    FILES_MATCHING
    PATTERN "*.h"
  )
  install(
    DIRECTORY "3rdparty/HalideIR/src/." DESTINATION "include/HalideIR"
    FILES_MATCHING
    PATTERN "*.h"
  )
  install(
    DIRECTORY "3rdparty/dlpack/include/." DESTINATION "include"
    FILES_MATCHING
    PATTERN "*.h"
    )
  install(
    DIRECTORY "nnvm/include/." DESTINATION "include"
    FILES_MATCHING
    PATTERN "*.h"
    )
else(INSTALL_DEV)
  install(
    DIRECTORY "include/tvm/runtime/." DESTINATION "include/tvm/runtime"
    FILES_MATCHING
    PATTERN "*.h"
    )
endif(INSTALL_DEV)

# https://docs.tvm.ai/install/from_source.html#python-package-installation
install(
    DIRECTORY python/ topi/python/ nnvm/python/
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/python/site-packages"
    FILES_MATCHING
    PATTERN "*"
    REGEX "\.gitignore$" EXCLUDE
    REGEX "setup\.py$" EXCLUDE
)

# More target definitions
if(MSVC)
  if(NOT TVM_BUILD_RUNTIME_ONLY)
  target_compile_definitions(tvm PRIVATE -DHalide_EXPORTS)
  endif()
  target_compile_definitions(tvm_runtime PRIVATE -DHalide_EXPORTS)
  if(NOT TVM_BUILD_RUNTIME_ONLY)
  target_compile_definitions(tvm PRIVATE -DTVM_EXPORTS)
  endif()
  target_compile_definitions(tvm_runtime PRIVATE -DTVM_EXPORTS)
  if(NOT TVM_BUILD_RUNTIME_ONLY)
  target_compile_definitions(nnvm_compiler PRIVATE -DNNVM_EXPORTS)
  endif()
endif()
