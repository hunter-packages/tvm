# Plugin rules for cblas
file(GLOB CBLAS_CONTRIB_SRC src/contrib/cblas/*.cc)

if(USE_BLAS STREQUAL "openblas")
  find_library(BLAS_LIBRARY openblas)
  list(APPEND TVM_RUNTIME_LINKER_LIBS ${BLAS_LIBRARY})
  list(APPEND RUNTIME_SRCS ${CBLAS_CONTRIB_SRC})
  message(STATUS "Use BLAS library " ${BLAS_LIBRARY})
elseif(USE_BLAS STREQUAL "mkl")
  if(NOT IS_DIRECTORY ${USE_MKL_PATH})
    set(USE_MKL_PATH /opt/intel/mkl)
  endif()
  find_library(BLAS_LIBRARY mkl_rt ${USE_MKL_PATH}/lib/ ${USE_MKL_PATH}/lib/intel64)
  include_directories(${USE_MKL_PATH}/include)
  list(APPEND TVM_RUNTIME_LINKER_LIBS ${BLAS_LIBRARY})
  list(APPEND RUNTIME_SRCS ${CBLAS_CONTRIB_SRC})
  add_definitions(-DUSE_MKL_BLAS=1)
  message(STATUS "Use BLAS library " ${BLAS_LIBRARY})
elseif(USE_BLAS STREQUAL "atlas" OR USE_BLAS STREQUAL "blas")
  find_library(BLAS_LIBRARY cblas)
  list(APPEND TVM_RUNTIME_LINKER_LIBS ${BLAS_LIBRARY})
  list(APPEND RUNTIME_SRCS ${CBLAS_CONTRIB_SRC})
  message(STATUS "Use BLAS library " ${BLAS_LIBRARY})
elseif(USE_BLAS STREQUAL "apple")
  if(HUNTER_ENABLED)
    find_package(accelerate REQUIRED)
    set(BLAS_LIBRARY accelerate::accelerate)
  else()
  find_library(BLAS_LIBRARY Accelerate)
  include_directories(${BLAS_LIBRARY}/Versions/Current/Frameworks/vecLib.framework/Versions/Current/Headers/)
  endif()
  list(APPEND TVM_RUNTIME_LINKER_LIBS ${BLAS_LIBRARY})
  list(APPEND RUNTIME_SRCS ${CBLAS_CONTRIB_SRC})
  message(STATUS "Use BLAS library " ${BLAS_LIBRARY})
elseif(USE_BLAS STREQUAL "none")
  # pass
else()
  message(FATAL_ERROR "Invalid option: USE_BLAS=" ${USE_BLAS})
endif()
