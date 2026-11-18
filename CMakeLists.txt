# Azure Kinect body tracking from an xblick-azure-kinect node's streams (README.md).
#
#   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="<xblick>;<zenoh>;<k4a SDK>;<nlohmann_json>"
#         -Dk4abt_DIR=<Body Tracking SDK>/usr/lib/cmake/k4abt
#         [-DXBLICK_K4A_ONNXRUNTIME_DIR=<ONNX Runtime> -DCUDAToolkit_ROOT=<CUDA toolkit>
#          -DXBLICK_K4A_CUDNN_DIR=<cuDNN> -DXBLICK_K4A_TENSORRT_DIR=<TensorRT>]
#         -DK4ABT_TEST_MODEL=<Body Tracking SDK>/usr/bin/dnn_model_2_0_op11.onnx
#
# Targets: xblick-k4abt (the tracker), test_k4a_body_tracking (no camera needed; ctest). Installs the library as
# the CMake package xblick_k4abt_lib (xblick_k4abt_lib::xblick_k4abt), for programs that embed the tracker.
cmake_minimum_required(VERSION 3.22)
project(xblick_k4abt VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# installed programs find this project's shared libraries (lib/) and keep the directories of the libraries they link
# (the SDKs, ONNX Runtime, CUDA), as the built ones do
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

# Durchblick's 0.5 line from 0.5.5 (its CMake package accepts its own minor version only, so 0.5.5 means 0.5.5 or a
# later 0.5.x; 0.5.5 brings xblick::cache_dir for the TensorRT engine cache): Options, Parameters, diagnostics
find_package(xblick 0.5.5 REQUIRED)
# libk4a for the SDK's types and its calibration functions; the tracker opens no device and needs neither the depth
# engine nor k4arecord
find_package(k4a 1.4 REQUIRED)
find_package(k4abt 1.1 REQUIRED)
find_package(nlohmann_json 3 REQUIRED)

# the ONNX Runtime the tracker runs on: a current one (for CUDA 12 or 13, e.g. onnxruntime-linux-x64-gpu_cuda12-1.31.1;
# needed for GPUs of compute capability 10 and newer) behind libonnxruntime.so.1.10.0 of src/onnxruntime_compat.cpp,
# or, without XBLICK_K4A_ONNXRUNTIME_DIR, the SDK's own 1.10 (CUDA 11)
set(XBLICK_K4A_ONNXRUNTIME_DIR "" CACHE PATH "ONNX Runtime package for body tracking (its lib/libonnxruntime.so.1)")
if(XBLICK_K4A_ONNXRUNTIME_DIR)
  if(NOT EXISTS "${XBLICK_K4A_ONNXRUNTIME_DIR}/lib/libonnxruntime.so.1")
    message(FATAL_ERROR "XBLICK_K4A_ONNXRUNTIME_DIR (${XBLICK_K4A_ONNXRUNTIME_DIR}) has no lib/libonnxruntime.so.1")
  endif()
  # the CUDA libraries that ONNX Runtime's CUDA and TensorRT providers load by name (cudart, cuBLAS, cuFFT, ...), of
  # the CUDA major version the ONNX Runtime package is built for
  find_package(CUDAToolkit 12 REQUIRED)
  add_library(onnxruntime_compat SHARED src/onnxruntime_compat.cpp)
  set_target_properties(onnxruntime_compat PROPERTIES OUTPUT_NAME onnxruntime SOVERSION 1.10.0
                        BUILD_RPATH "${XBLICK_K4A_ONNXRUNTIME_DIR}/lib"
                        INSTALL_RPATH "${XBLICK_K4A_ONNXRUNTIME_DIR}/lib")
  target_link_options(onnxruntime_compat PRIVATE
                      "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/onnxruntime_compat.map")
  target_include_directories(onnxruntime_compat PRIVATE "${XBLICK_K4A_ONNXRUNTIME_DIR}/include")
  target_link_libraries(onnxruntime_compat PRIVATE ${CMAKE_DL_LIBS})
  # the executables find it through DT_RPATH, which comes before LD_LIBRARY_PATH (RUNPATH comes after it): the SDK's
  # own libonnxruntime.so.1.10.0 is usually on LD_LIBRARY_PATH
  target_link_options(onnxruntime_compat INTERFACE "-Wl,--disable-new-dtags")
  # ONNX Runtime's providers have no RUNPATH: they find the CUDA libraries, cuDNN and TensorRT through the
  # executable's DT_RPATH
  target_link_options(onnxruntime_compat INTERFACE "-Wl,-rpath,${CUDAToolkit_LIBRARY_DIR}")
  # TensorRT for --processing tensorrt; without it, TensorRT has to be on LD_LIBRARY_PATH
  set(XBLICK_K4A_TENSORRT_DIR "" CACHE PATH "TensorRT 10 package for body tracking (its lib/libnvinfer.so.10)")
  if(XBLICK_K4A_TENSORRT_DIR)
    if(NOT EXISTS "${XBLICK_K4A_TENSORRT_DIR}/lib/libnvinfer.so.10")
      message(FATAL_ERROR "XBLICK_K4A_TENSORRT_DIR (${XBLICK_K4A_TENSORRT_DIR}) has no lib/libnvinfer.so.10")
    endif()
    target_link_options(onnxruntime_compat INTERFACE "-Wl,-rpath,${XBLICK_K4A_TENSORRT_DIR}/lib")
  endif()
  # cuDNN 9 for the CUDA provider, the same way, where the CUDA toolkit has none (NVIDIA's toolkit does not bring
  # it); without it, cuDNN has to be in the toolkit's library directory or on LD_LIBRARY_PATH
  set(XBLICK_K4A_CUDNN_DIR "" CACHE PATH "cuDNN 9 package for body tracking (its lib/libcudnn.so.9)")
  if(XBLICK_K4A_CUDNN_DIR)
    if(NOT EXISTS "${XBLICK_K4A_CUDNN_DIR}/lib/libcudnn.so.9")
      message(FATAL_ERROR "XBLICK_K4A_CUDNN_DIR (${XBLICK_K4A_CUDNN_DIR}) has no lib/libcudnn.so.9")
    endif()
    target_link_options(onnxruntime_compat INTERFACE "-Wl,-rpath,${XBLICK_K4A_CUDNN_DIR}/lib")
  endif()
  set(K4ABT_ONNXRUNTIME onnxruntime_compat)
else()
  find_file(K4ABT_ONNXRUNTIME libonnxruntime.so.1.10.0 PATHS "${k4abt_DIR}/../.." NO_DEFAULT_PATH REQUIRED)
endif()

# src/calibration.* is the reading side of xblick-azure-kinect's calibration record, in the namespace
# xblick::k4a_record, apart from the camera's own in a program that links both
add_library(xblick_k4abt STATIC src/body_tracking.cpp src/calibration.cpp)
target_include_directories(xblick_k4abt PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
                                               $<INSTALL_INTERFACE:include/xblick_k4abt>)
target_link_libraries(xblick_k4abt PUBLIC xblick::xblick k4a::k4a k4abt::k4abt ${K4ABT_ONNXRUNTIME}
                                   PRIVATE nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})
if(XBLICK_K4A_ONNXRUNTIME_DIR)
  target_compile_definitions(xblick_k4abt PUBLIC XBLICK_K4A_ONNXRUNTIME_COMPAT)
endif()
target_compile_options(xblick_k4abt PRIVATE -Wall -Wextra)

add_executable(xblick-k4abt src/main_body_tracking.cpp)
target_link_libraries(xblick-k4abt PRIVATE xblick_k4abt)
install(TARGETS xblick-k4abt RUNTIME DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
  add_executable(test_k4a_body_tracking tests/test_body_tracking.cpp)
  target_link_libraries(test_k4a_body_tracking PRIVATE xblick_k4abt nlohmann_json::nlohmann_json)
  target_compile_definitions(test_k4a_body_tracking PRIVATE
    K4A_TEST_CALIBRATION="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/k4a_test_calibration.json")
  # the SDK's model dnn_model_2_0_op11.onnx (its lite model does not find the test's mannequin)
  set(K4ABT_TEST_MODEL "" CACHE FILEPATH "Body Tracking SDK model for the body_tracking test")
  set(K4ABT_TEST_PROCESSING "cpu" CACHE STRING "processing mode of the body_tracking test: cpu, cuda, tensorrt")
  if(NOT K4ABT_TEST_MODEL)
    message(FATAL_ERROR "the tests need K4ABT_TEST_MODEL (the SDK's .onnx model), or -DBUILD_TESTING=OFF")
  endif()
  add_test(NAME body_tracking COMMAND test_k4a_body_tracking ${K4ABT_TEST_MODEL} ${K4ABT_TEST_PROCESSING})
endif()

# The CMake package xblick_k4abt_lib: the tracker's library with its header and the session helpers
# (body_tracking.hpp, session.hpp). Named apart from projects named xblick_k4abt that build on it and export a
# package of their own name.
set(XBLICK_K4ABT_EXPORTED xblick_k4abt)
if(XBLICK_K4A_ONNXRUNTIME_DIR)
  list(APPEND XBLICK_K4ABT_EXPORTED onnxruntime_compat)
endif()
install(TARGETS ${XBLICK_K4ABT_EXPORTED} EXPORT xblick_k4abt_libTargets
        ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(FILES src/body_tracking.hpp src/session.hpp DESTINATION include/xblick_k4abt)
install(EXPORT xblick_k4abt_libTargets NAMESPACE xblick_k4abt_lib:: DESTINATION lib/cmake/xblick_k4abt_lib)
if(XBLICK_K4A_ONNXRUNTIME_DIR)
  set(XBLICK_K4ABT_COMPAT ON)
else()
  set(XBLICK_K4ABT_COMPAT OFF)
endif()
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/xblick_k4abt_libConfig.cmake.in
                              ${CMAKE_CURRENT_BINARY_DIR}/xblick_k4abt_libConfig.cmake
                              INSTALL_DESTINATION lib/cmake/xblick_k4abt_lib)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/xblick_k4abt_libConfigVersion.cmake
                                 COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/xblick_k4abt_libConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/xblick_k4abt_libConfigVersion.cmake
        DESTINATION lib/cmake/xblick_k4abt_lib)
