# Build and test xblick-k4abt

This page lists the dependencies, what `tools/setup.sh` installs and from where, the manual build with CMake, the
CMake options and the test.

## Dependencies

| Dependency | Needed for |
| --- | --- |
| CMake 3.24 or newer (Durchblick needs it), Ninja, a C++20 compiler | the build |
| Durchblick's C++ package (`find_package(xblick 0.5.5)`), version 0.5.5 or a later 0.5 | everything |
| zenoh-c and zenoh-cpp 1.10 | everything |
| Azure Kinect Sensor SDK 1.4 (`find_package(k4a)`) | the SDK's types and calibration functions |
| Body Tracking SDK 1.1 (`find_package(k4abt)`) | everything |
| nlohmann_json | everything |
| libjpeg or libjpeg-turbo, libpng (Durchblick needs them) | everything |
| CUDA toolkit 12 or 13 (`find_package(CUDAToolkit)`) | ONNX Runtime's CUDA libraries |
| ONNX Runtime for the toolkit's CUDA version, Microsoft's release package, 1.31.1 in the setup script | the tracker's network |
| cuDNN 9 for the toolkit's CUDA version | ONNX Runtime's CUDA provider |
| TensorRT 10 for the toolkit's CUDA version | TensorRT mode |

The tracker opens no device. It needs neither `k4arecord` nor the depth engine. `libk4a` still loads the depth engine
when it starts and logs three errors when it cannot ([design.md](design.md#libraries-at-run-time)).

Microsoft ships the depth engine and the Body Tracking SDK as binary packages under its own licence terms. This
repository contains neither.

## The setup script

`tools/setup.sh` installs everything in two places. It does not use `sudo` and writes nothing outside them.

| Place | Content |
| --- | --- |
| Durchblick's cache directory `cache_dir("k4abt")`: `$XBLICK_HOME/cache/k4abt`, else `$XDG_CACHE_HOME/xblick/k4abt`, else `~/.cache/xblick/k4abt` | the prerequisites, one directory per package and version; `downloads/` with the downloaded files; `src/` and `build/` of the packages that the script builds. The tracker's TensorRT engine cache is in the same directory. |
| the repository | `build-setup/`, the build of the tracker; `install/`, the installed program and `install/env.sh` |

`install/env.sh` puts the tracker, Durchblick's C++ programs and `zenohd` on `PATH` and sets
`K4A_BODY_TRACKING_DNN_MODEL_FILEPATH` to the SDK's model. It sets no `LD_LIBRARY_PATH`, because the program finds
every library through its RPATH.

The script downloads these files. It checks each one against its sha256 and stops on a mismatch.

| Package | Version | Source | Terms |
| --- | --- | --- | --- |
| zenoh-c, standalone build | 1.10.1 | github.com/eclipse-zenoh/zenoh-c, release | EPL 2.0 or Apache 2.0 |
| zenoh-cpp, source | 1.10.1 | github.com/eclipse-zenoh/zenoh-cpp, tag | EPL 2.0 or Apache 2.0 |
| `zenohd`, the router | 1.10.1 | github.com/eclipse-zenoh/zenoh, release | EPL 2.0 or Apache 2.0 |
| Durchblick, source | 0.5.12 | github.com/xblick/xblick, tag `v0.5.12` | Apache 2.0 |
| nlohmann_json | 3.12.0 | github.com/nlohmann/json, release | MIT |
| `libk4a1.4`, `libk4a1.4-dev`: Sensor SDK with the depth engine | 1.4.1 | packages.microsoft.com, `ubuntu/18.04/prod` | [Microsoft Software License Terms, Azure Kinect Sensor SDK](https://www.nuget.org/packages/Microsoft.Azure.Kinect.Sensor/1.4.1/License) |
| `libk4abt1.1`, `libk4abt1.1-dev`: Body Tracking SDK with its models (1.8 GB) | 1.1.2 | packages.microsoft.com, `ubuntu/18.04/prod` | [Microsoft Software License Terms, Azure Kinect Body Tracking SDK](https://www.nuget.org/packages/Microsoft.Azure.Kinect.BodyTracking/1.1.2/License) |
| ONNX Runtime for CUDA 12 (450 MB) or for CUDA 13 | 1.31.1 | github.com/microsoft/onnxruntime, release | MIT |
| cuDNN for CUDA 12 (880 MB) or for CUDA 13 | 9.15.0.57 (CUDA 12), 9.15.0.58 (CUDA 13) | developer.download.nvidia.com, `cudnn/redist` | [NVIDIA cuDNN Software License Agreement](https://docs.nvidia.com/deeplearning/cudnn/latest/reference/eula.html) |
| TensorRT for CUDA 12.9 (7.9 GB) or for CUDA 13.0 | 10.14.1.48 | developer.download.nvidia.com, `machine-learning/tensorrt` | [NVIDIA TensorRT Software License Agreement](https://docs.nvidia.com/deeplearning/tensorrt/latest/reference/sla.html) |

The script takes the builds of ONNX Runtime, cuDNN and TensorRT for the major version of the CUDA toolkit: those for
CUDA 12 with a CUDA 12 toolkit, those for CUDA 13 with a CUDA 13 toolkit. The builds load the toolkit's libraries by
name, so no other combination works. The script prints the terms' name and link when it starts the download of a file
that is not under an open-source licence. It unpacks Microsoft's packages with `dpkg-deb -x` and does not install
them. Microsoft's packages ask for the acceptance of the licence terms only when `dpkg` or `apt` installs them.

A file that is already in `downloads/` under the name that the script uses is not downloaded again. A file obtained
in another way, for example on a PC without internet access, can therefore be put there. The script checks its
sha256 in the same way. Files that `tools/setup.sh` of xblick-azure-kinect 0.3 downloaded for `--body-tracking` are
in `~/.cache/xblick/azure-kinect/downloads/` and can be moved here.

What needs root, the script checks and leaves to you:

| What | Check | When it is missing |
| --- | --- | --- |
| Ubuntu packages: `cmake ninja-build g++ curl unzip xz-utils libjpeg-turbo8-dev libpng-dev libgl1 libx11-6 libudev1 libgomp1` | `dpkg-query` | The script stops and prints `sudo apt install` with the missing packages. |
| A CUDA toolkit, 12 or 13 | `<toolkit>/version.json`, `bin/nvcc`, `include/cuda.h`, the cuBLAS library | The script stops and names the package `cuda-toolkit-12-8` of NVIDIA's repository and `--cuda-toolkit DIR`. Another major version stops it too. |

| Option | Effect |
| --- | --- |
| `--cuda-toolkit DIR` | The CUDA toolkit, 12 or 13. The default is `/usr/local/cuda`. |

The script builds Durchblick without its point cloud node and takes `cuda.h` from the toolkit.

A second call does only what changed. Each package directory has a stamp `.setup-done`. The tracker is configured
again when the options change, built when a file changed, and tested after each build. The logs of the tracker's steps
are `build-setup/.setup-configure.log`, `.setup-build.log` and `.setup-ctest.log`.

## Manual build with CMake

Replace each `<...>` with the install prefix of that package.

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="<xblick>;<zenoh>;<k4a>/usr;<nlohmann_json>" \
  -Dk4abt_DIR=<body tracking>/usr/lib/cmake/k4abt -DCUDAToolkit_ROOT=<cuda-12.8> \
  -DXBLICK_K4A_ONNXRUNTIME_DIR=<onnxruntime-linux-x64-gpu_cuda12-1.31.1> \
  -DXBLICK_K4A_CUDNN_DIR=<cudnn-linux-x86_64-9.15.0.57_cuda12-archive> \
  -DXBLICK_K4A_TENSORRT_DIR=<TensorRT-10.14.1.48> \
  -DK4ABT_TEST_MODEL=<body tracking>/usr/bin/dnn_model_2_0_op11.onnx -DK4ABT_TEST_PROCESSING=cuda
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix <xblick_k4abt_lib>   # the CMake package xblick_k4abt_lib, for other projects
```

Here `<k4a>` and `<body tracking>` are directories into which Microsoft's packages were unpacked, or `/` after `apt`
installed them. With a CUDA 13 toolkit, ONNX Runtime, cuDNN and TensorRT have to be their builds for CUDA 13.

Build in a directory on a local disk. Do not build on a shared filesystem.

## CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `k4abt_DIR` | | `<body tracking>/usr/lib/cmake/k4abt` |
| `XBLICK_K4A_ONNXRUNTIME_DIR` | | The directory of a current ONNX Runtime, for example `onnxruntime-linux-x64-gpu_cuda12-1.31.1`. Needs `CUDAToolkit_ROOT`. Without it, the tracker runs on the SDK's own ONNX Runtime 1.10, which refuses GPUs of compute capability 10 and newer and has no TensorRT engine cache. |
| `XBLICK_K4A_CUDNN_DIR` | | The directory of cuDNN 9, for example `cudnn-linux-x86_64-9.15.0.57_cuda12-archive`. The program then finds cuDNN through its RPATH. Without it, cuDNN has to be in the CUDA toolkit's library directory. |
| `XBLICK_K4A_TENSORRT_DIR` | | The directory of TensorRT, for example `TensorRT-10.14.1.48`. The program then finds TensorRT through its RPATH. |
| `K4ABT_TEST_MODEL` | | The model for the `body_tracking` test: `<body tracking>/usr/bin/dnn_model_2_0_op11.onnx`. Required with the tests. |
| `K4ABT_TEST_PROCESSING` | `cpu` | The processing mode of that test: `cpu`, `cuda` or `tensorrt`. |

The installed package `xblick_k4abt_lib` has the target `xblick_k4abt_lib::xblick_k4abt` with the headers
`body_tracking.hpp` and `session.hpp`, and the variable `xblick_k4abt_lib_ONNXRUNTIME_COMPAT`.

## Test

The test needs no camera.

| Test | What it checks |
| --- | --- |
| `body_tracking` | A stand-in for the camera node publishes depth and IR of a rendered mannequin and the calibration record, as `xblick-azure-kinect` does. The tracker receives them over Zenoh. The test checks the results on `<base>/bodies` and their attachments, compares the joints with the SDK's own processing, and checks the settings while the tracker runs (off, masks, rate limit) and the diagnostics. |

The test needs the SDK's full model `dnn_model_2_0_op11.onnx`. The lite model finds nothing in the rendered
mannequin.

`test_k4a_body_tracking --publish ENDPOINT BASE` runs only the stand-in camera, listening on `ENDPOINT`, for a tracker
in another process.
