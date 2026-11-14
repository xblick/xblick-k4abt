# Build and test xblick-azure-kinect

This page lists the dependencies, the CMake options and the tests. The README has the short build command.

## Dependencies

| Dependency | Needed for |
| --- | --- |
| Durchblick's C++ package (`find_package(xblick 0.5.5)`), version 0.5.5 or a later 0.5 | everything |
| zenoh-c and zenoh-cpp 1.10 | everything |
| Azure Kinect Sensor SDK (`find_package(k4a)`, `k4arecord`) | everything |
| nlohmann_json | everything |
| libjpeg or libjpeg-turbo | everything |
| CUDA 12 toolkit with nvJPEG | optional: decoding on the GPU |
| Body Tracking SDK 1.1 | optional: `xblick-k4abt` |
| ONNX Runtime for CUDA 12, Microsoft's release package (tested: 1.31.1) | body tracking on GPUs of compute capability 10 and newer |
| TensorRT 10 (tested: 10.14.1) | body tracking in TensorRT mode |

Microsoft ships the depth engine and the Body Tracking SDK as binary packages under its own licence terms. You
install them separately. This repository contains neither.

The programs use the Zenoh router of the PC as their default session. When Durchblick has its point cloud node, the
`offline` test also checks that node against the SDK.

## CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `XBLICK_K4A_CUDA` | `OFF` | Decode with nvJPEG and rectify with NPP on the GPU. Needs `CUDAToolkit_ROOT`. |
| `XBLICK_K4A_BODY_TRACKING` | `ON` when `k4abt_DIR` is in the environment | Build `xblick-k4abt`. |
| `k4abt_DIR` | | `<body tracking>/usr/lib/cmake/k4abt` |
| `XBLICK_K4A_ONNXRUNTIME_DIR` | | The directory of a current ONNX Runtime, for example `onnxruntime-linux-x64-gpu_cuda12-1.31.1`. |
| `XBLICK_K4A_TENSORRT_DIR` | | The directory of TensorRT, for example `TensorRT-10.14.1.48`. The programs then find TensorRT through their RPATH. |
| `K4ABT_TEST_MODEL` | | The model for the `body_tracking` test: `<body tracking>/usr/bin/dnn_model_2_0_op11.onnx`. |
| `K4ABT_TEST_PROCESSING` | | The processing mode of that test: `cpu`, `cuda` or `tensorrt`. |

The full command with body tracking:

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="<xblick>;<zenoh>;<k4a>;<nlohmann_json>" \
  -DXBLICK_K4A_CUDA=ON -DCUDAToolkit_ROOT=<cuda-12.8> \
  -DXBLICK_K4A_BODY_TRACKING=ON -Dk4abt_DIR=<body tracking>/usr/lib/cmake/k4abt \
  -DXBLICK_K4A_ONNXRUNTIME_DIR=<onnxruntime-linux-x64-gpu_cuda12-1.31.1> \
  -DXBLICK_K4A_TENSORRT_DIR=<TensorRT-10.14.1.48> \
  -DK4ABT_TEST_MODEL=<body tracking>/usr/bin/dnn_model_2_0_op11.onnx -DK4ABT_TEST_PROCESSING=cuda
cmake --build build
ctest --test-dir build
cmake --install build --prefix <xblick_k4a>
```

`ctest` needs these libraries on `LD_LIBRARY_PATH`: k4a with the depth engine, zenoh and k4abt.

Build in a directory on a local disk. Do not build on a shared filesystem.

## Tests

The tests need no camera.

| Test | What it checks |
| --- | --- |
| `offline` | The clock. The calibration, from the SDK's test calibration. The MJPEG handling. The node end to end over Zenoh. nvJPEG against libjpeg, also through the CUDA ring into another process. `depth_to_color` from the depth engine in its EGL context, against the SDK's CPU projection. `color_rect` against the SDK's projection. Durchblick's point cloud node against the SDK's projection. The playback of a recording that the SDK writes from such captures, also in a loop, with its stamps. |
| `parameters` | The parameters, the parameter types and the clock estimators on synthetic delays. |
| `body_tracking` | The tracker, which receives a rendered mannequin from the node's streams. The test checks the results on `<base>/bodies` and compares the joints with the SDK's own processing. |

The `body_tracking` test needs the SDK's full model `dnn_model_2_0_op11.onnx`. The lite model finds nothing in the
rendered mannequin.

The `offline` test reads the CUDA ring from `test_k4a_cuda_reader`, a separate process, because the process that
created a CUDA ring cannot open it.

## Tools

| Tool | Purpose |
| --- | --- |
| `tools/k4a_node_bench.cpp` | The cost per capture with subscribers. |
| `tools/k4a_depth_to_color_check.cpp` | Compares a running node's `depth_to_color` with the SDK's transformation. |
| `tools/k4a_depth_to_color_mirror.cpp` | Publishes the other method's `depth_to_color` below another base. |
