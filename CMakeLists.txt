# Azure Kinect cameras as Durchblick streams (README.md).
#
#   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="<xblick>;<zenoh>;<k4a SDK>;<nlohmann_json>"
#         [-DXBLICK_K4A_CUDA=ON -DCUDAToolkit_ROOT=<CUDA toolkit with nvJPEG>]
#         [-DXBLICK_K4A_BODY_TRACKING=ON -Dk4abt_DIR=<Body Tracking SDK>/usr/lib/cmake/k4abt]
#
# Targets: xblick-azure-kinect (the camera node), xblick-k4abt (body tracking from the node's streams, with
# XBLICK_K4A_BODY_TRACKING), test_k4a_offline (no camera needed; ctest). Installs the libraries as the CMake package
# xblick_k4a (xblick_k4a::xblick_k4a, xblick_k4a::xblick_k4abt), for programs that build on the
# node and the tracker.
cmake_minimum_required(VERSION 3.22)
project(xblick_azure_kinect VERSION 0.3.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# installed programs find this project's shared libraries (lib/) and keep the directories of the libraries they link
# (CUDA, the SDKs), as the built ones do
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

option(XBLICK_K4A_CUDA "decode (nvJPEG) and rectify (NPP) the colour camera's MJPEG on the GPU (CUDA toolkit)" OFF)
# on by default where the Body Tracking SDK is in the environment (environment variable k4abt_DIR); on without the
# SDK, the build stops at find_package(k4abt)
if(DEFINED ENV{k4abt_DIR})
  set(xblick_k4a_body_tracking_default ON)
else()
  set(xblick_k4a_body_tracking_default OFF)
endif()
option(XBLICK_K4A_BODY_TRACKING "build xblick-k4abt (Azure Kinect Body Tracking SDK 1.1)"
       ${xblick_k4a_body_tracking_default})
message(STATUS "xblick_k4a: body tracking ${XBLICK_K4A_BODY_TRACKING}")

# Durchblick's 0.5 line from 0.5.5 (its CMake package accepts its own minor version only, so 0.5.5 means 0.5.5 or a
# later 0.5.x; 0.5.5 brings xblick::cache_dir for the TensorRT engine cache): publish_jpeg, pin_host,
# StaticTransformPublisher, Imu, diagnostics, CameraInfo::read_file, Options, Parameters, RVL for depth, ring keys with
# the host; point clouds (the tests use its point cloud node xblick-points when Durchblick was built with it)
find_package(xblick 0.5.5 REQUIRED OPTIONAL_COMPONENTS points)
find_package(k4a 1.4 REQUIRED)
find_package(k4arecord 1.4 REQUIRED)
find_package(nlohmann_json 3 REQUIRED)
find_package(JPEG REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS EGL)

# the depth engine's OpenGL context through EGL, without an X server (src/egl_glx.cpp); shared, so that its Xlib/GLX
# functions are found before libX11's and libGL's
add_library(xblick_k4a_egl SHARED src/egl_glx.cpp)
target_include_directories(xblick_k4a_egl PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
                                                 $<INSTALL_INTERFACE:include/xblick_k4a>)
target_link_libraries(xblick_k4a_egl PRIVATE OpenGL::EGL)
target_compile_options(xblick_k4a_egl PRIVATE -Wall -Wextra)

add_library(xblick_k4a STATIC
  src/clock.cpp
  src/calibration.cpp
  src/jpeg.cpp
  src/source.cpp
  src/depth_to_color.cpp
  src/node.cpp
  src/rectify.cpp
  src/settings.cpp)
target_include_directories(xblick_k4a PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
                                             $<INSTALL_INTERFACE:include/xblick_k4a>)
target_link_libraries(xblick_k4a PUBLIC xblick::xblick k4a::k4a k4a::k4arecord
                                        nlohmann_json::nlohmann_json JPEG::JPEG xblick_k4a_egl)
target_compile_options(xblick_k4a PRIVATE -Wall -Wextra)
if(XBLICK_K4A_CUDA)
  find_package(CUDAToolkit 12 REQUIRED)
  target_sources(xblick_k4a PRIVATE src/nvjpeg_decoder.cpp)
  target_compile_definitions(xblick_k4a PUBLIC XBLICK_K4A_CUDA)
  # nppig: the rectified colour's remap (rectify.cpp)
  target_link_libraries(xblick_k4a PUBLIC CUDA::cudart CUDA::nvjpeg CUDA::nppig CUDA::nppc)
  # the node's cost per capture with shared-memory and CUDA subscribers on every stream (tools/k4a_node_bench.cpp)
  add_executable(k4a_node_bench tools/k4a_node_bench.cpp)
  target_link_libraries(k4a_node_bench PRIVATE xblick_k4a)
endif()

add_executable(xblick-azure-kinect src/main_camera.cpp)
target_link_libraries(xblick-azure-kinect PRIVATE xblick_k4a)

# a node's parameters over its ROS 2 parameter services (as ros2 param), the camera's and any other node's
add_executable(xblick-azure-kinect-parameters src/main_parameters.cpp)
target_link_libraries(xblick-azure-kinect-parameters PRIVATE xblick_k4a)

# a running node's depth_to_color against the SDK's transformation of the same depth frames, live
# (tools/k4a_depth_to_color_check.cpp)
add_executable(k4a_depth_to_color_check tools/k4a_depth_to_color_check.cpp)
target_link_libraries(k4a_depth_to_color_check PRIVATE xblick_k4a)
# a running node's depth frames turned into depth_to_color another way (the SDK's, ...), published below another base,
# to look at both side by side (tools/k4a_depth_to_color_mirror.cpp)
add_executable(k4a_depth_to_color_mirror tools/k4a_depth_to_color_mirror.cpp)
target_link_libraries(k4a_depth_to_color_mirror PRIVATE xblick_k4a)

if(XBLICK_K4A_BODY_TRACKING)
  find_package(k4abt 1.1 REQUIRED)
  # the ONNX Runtime the tracker runs on: a current one (CUDA 12, e.g. onnxruntime-linux-x64-gpu_cuda12-1.31.1; needed
  # for GPUs of compute capability 10 and newer) behind libonnxruntime.so.1.10.0 of src/onnxruntime_compat.cpp, or,
  # without XBLICK_K4A_ONNXRUNTIME_DIR, the SDK's own 1.10 (CUDA 11)
  set(XBLICK_K4A_ONNXRUNTIME_DIR "" CACHE PATH
      "ONNX Runtime package for body tracking (its lib/libonnxruntime.so.1)")
  if(XBLICK_K4A_ONNXRUNTIME_DIR)
    if(NOT EXISTS "${XBLICK_K4A_ONNXRUNTIME_DIR}/lib/libonnxruntime.so.1")
      message(FATAL_ERROR
              "XBLICK_K4A_ONNXRUNTIME_DIR (${XBLICK_K4A_ONNXRUNTIME_DIR}) has no lib/libonnxruntime.so.1")
    endif()
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
    # TensorRT for --processing tensorrt: ONNX Runtime's providers have no RUNPATH, so they find libnvinfer through the
    # executable's DT_RPATH; without it, TensorRT has to be on LD_LIBRARY_PATH
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
  add_library(xblick_k4abt STATIC src/body_tracking.cpp)
  target_link_libraries(xblick_k4abt PUBLIC xblick_k4a k4abt::k4abt ${K4ABT_ONNXRUNTIME}
                                     PRIVATE ${CMAKE_DL_LIBS})
  if(XBLICK_K4A_ONNXRUNTIME_DIR)
    target_compile_definitions(xblick_k4abt PUBLIC XBLICK_K4A_ONNXRUNTIME_COMPAT)
  endif()
  target_compile_options(xblick_k4abt PRIVATE -Wall -Wextra)
  add_executable(xblick-k4abt src/main_body_tracking.cpp)
  target_link_libraries(xblick-k4abt PRIVATE xblick_k4abt)
  install(TARGETS xblick-k4abt RUNTIME DESTINATION bin)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(test_k4a_offline tests/test_offline.cpp)
  target_link_libraries(test_k4a_offline PRIVATE xblick_k4a)
  target_compile_definitions(test_k4a_offline PRIVATE K4A_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}")
  if(XBLICK_K4A_CUDA)
    # the CUDA ring's frames are read in a process of their own (CUDA IPC handles do not open in their own process)
    add_executable(test_k4a_cuda_reader tests/cuda_reader.cpp)
    target_link_libraries(test_k4a_cuda_reader PRIVATE xblick::xblick CUDA::cuda_driver)
    target_compile_definitions(test_k4a_offline PRIVATE K4A_CUDA_READER="$<TARGET_FILE:test_k4a_cuda_reader>")
    add_dependencies(test_k4a_offline test_k4a_cuda_reader)
  endif()
  target_compile_definitions(test_k4a_offline PRIVATE
    K4A_TEST_CALIBRATION="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/k4a_test_calibration.json")
  if(xblick_points_FOUND)
    # Durchblick's point cloud node against the SDK's projection (it needs a GPU)
    find_program(K4A_POINTS_PROGRAM xblick-points HINTS "${xblick_DIR}/../../../bin" NO_DEFAULT_PATH REQUIRED)
    target_compile_definitions(test_k4a_offline PRIVATE K4A_POINTS_PROGRAM="${K4A_POINTS_PROGRAM}")
  endif()
  add_test(NAME offline COMMAND test_k4a_offline)
  # the parameters (rcl_interfaces CDR against rosbags' bytes, the services, fake controls) and the clock's estimators
  add_executable(test_k4a_parameters tests/test_parameters.cpp)
  target_link_libraries(test_k4a_parameters PRIVATE xblick_k4a)
  add_test(NAME parameters COMMAND test_k4a_parameters)
  if(XBLICK_K4A_BODY_TRACKING)
    add_executable(test_k4a_body_tracking tests/test_body_tracking.cpp)
    target_link_libraries(test_k4a_body_tracking PRIVATE xblick_k4abt)
    target_compile_definitions(test_k4a_body_tracking PRIVATE
      K4A_TEST_CALIBRATION="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/k4a_test_calibration.json")
    # the SDK's model dnn_model_2_0_op11.onnx (its lite model does not find the test's mannequin)
    set(K4ABT_TEST_MODEL "" CACHE FILEPATH "Body Tracking SDK model for the body_tracking test")
    set(K4ABT_TEST_PROCESSING "cpu" CACHE STRING "processing mode of the body_tracking test: cpu, cuda, tensorrt")
    if(NOT K4ABT_TEST_MODEL)
      message(FATAL_ERROR "XBLICK_K4A_BODY_TRACKING with tests needs K4ABT_TEST_MODEL (the SDK's .onnx model)")
    endif()
    add_test(NAME body_tracking COMMAND test_k4a_body_tracking ${K4ABT_TEST_MODEL} ${K4ABT_TEST_PROCESSING})
  endif()
endif()

install(TARGETS xblick-azure-kinect xblick-azure-kinect-parameters RUNTIME DESTINATION bin)

# The CMake package xblick_k4a: the node's and the tracker's libraries with their headers (src/, without the
# programs' and the decoders' own).
set(XBLICK_K4A_EXPORTED xblick_k4a xblick_k4a_egl)
set(XBLICK_K4A_HEADERS src/calibration.hpp src/clock.hpp src/egl_glx.hpp src/node.hpp src/session.hpp
                       src/settings.hpp src/source.hpp)
if(XBLICK_K4A_BODY_TRACKING)
  list(APPEND XBLICK_K4A_EXPORTED xblick_k4abt)
  list(APPEND XBLICK_K4A_HEADERS src/body_tracking.hpp)
  if(XBLICK_K4A_ONNXRUNTIME_DIR)
    list(APPEND XBLICK_K4A_EXPORTED onnxruntime_compat)
  endif()
endif()
install(TARGETS ${XBLICK_K4A_EXPORTED} EXPORT xblick_k4aTargets
        ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(FILES ${XBLICK_K4A_HEADERS} DESTINATION include/xblick_k4a)
install(EXPORT xblick_k4aTargets NAMESPACE xblick_k4a:: DESTINATION lib/cmake/xblick_k4a)
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/xblick_k4aConfig.cmake.in
                              ${CMAKE_CURRENT_BINARY_DIR}/xblick_k4aConfig.cmake
                              INSTALL_DESTINATION lib/cmake/xblick_k4a)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/xblick_k4aConfigVersion.cmake
                                 COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/xblick_k4aConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/xblick_k4aConfigVersion.cmake
        DESTINATION lib/cmake/xblick_k4a)
