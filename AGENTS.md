# AGENTS.md

Azure Kinect body tracking as a Durchblick node: `xblick-k4abt` reads `<base>/depth`, `<base>/ir` and the calibration
record `<base>/k4a_calibration` of an `xblick-azure-kinect` node (github.com/xblick/xblick-azure-kinect) and publishes
`<base>/bodies`. C++20, plain CMake project `xblick_k4abt`, installs the CMake package `xblick_k4abt` for programs
that embed the tracker. Human documentation: `README.md`, then `docs/` (options, performance, design, build).
Transport-wide traps (Zenoh sessions and routers, rings, hanging tests): `AGENTS.md` of the core repository
(github.com/xblick/xblick). Style of all documentation: the core's `docs/project/writing-docs.md`.

## Commands

```bash
# Everything from scratch (prerequisites in xblick::cache_dir("k4abt"), build in build-setup/, ctest, install/ with
# install/env.sh). No sudo: it prints the apt command and stops. Options: --help. Details: docs/build.md.
tools/setup.sh [--cuda-toolkit DIR]
# By hand. Build on a local disk, never on a shared filesystem. <...> = install prefixes. Options: docs/build.md.
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="<xblick>;<zenoh>;<k4a>/usr;<nlohmann_json>" \
  -Dk4abt_DIR=<sdk>/usr/lib/cmake/k4abt -DCUDAToolkit_ROOT=<cuda-12.8> -DXBLICK_K4A_ONNXRUNTIME_DIR=... \
  -DXBLICK_K4A_CUDNN_DIR=... -DXBLICK_K4A_TENSORRT_DIR=... -DK4ABT_TEST_MODEL=<sdk>/usr/bin/dnn_model_2_0_op11.onnx
cmake --build build
ctest --test-dir build --output-on-failure   # no LD_LIBRARY_PATH needed
cmake --install build --prefix <xblick_k4abt>   # the CMake package xblick_k4abt (xblick_k4abt_DIR)
tools/check_references.sh                         # the reference check of the CI
```

Durations: the `body_tracking` test takes about a minute with TensorRT (the engine is built at every test). Give
`ctest` a timeout.

- The test needs no camera: `body_tracking`, with a stand-in for the camera node. Content: docs/build.md.
- It needs the SDK's full model `dnn_model_2_0_op11.onnx`. The lite model finds nothing.
  `-DK4ABT_TEST_PROCESSING=cpu|cuda|tensorrt`.

## Rules

- Fail fast. An unknown option, a missing model or an unsupported GPU stops the program with the cause.
- The program defines its options with Durchblick's `xblick::Options` (`--help` lists them), and every option is
  also a parameter of the node `<base>/k4abt`. Keep the header comment of `src/main_body_tracking.cpp` and
  docs/options.md in sync with them.
- Read docs/design.md before you change build or run-time details. Each decision has a reason there (ONNX Runtime
  stand-in, RPATH, the calibration copy, the package name). Keep that table current.
- Run-time libraries come from RPATH. Never put them on a workspace-wide `LD_LIBRARY_PATH`: that would shadow, for
  example, PyTorch's cuDNN for every program of the workspace. `install/env.sh` sets PATH and the model only.
- `tools/setup.sh` pins every download (file, URL, sha256 in `pins`). A new version needs its sha256 and the table
  in docs/build.md. No sudo in it: what needs root it checks and prints. Microsoft's and NVIDIA's binaries: print the
  terms' name and link when the download starts (no accept flag).
- `src/calibration.*` is a reduced copy of xblick-azure-kinect's, in the namespace `xblick::k4a_record`. The camera
  defines the record: change it there first, then here, and raise `kCalibrationRecordVersion` in both.
- A tracking failure ends the program.
- Documentation: Markdown only in `README.md` and `docs/`, one purpose per page, history in commit messages. State
  requirements, not what something was tried with.

## Traps

| Trap | Symptom | Fix |
| --- | --- | --- |
| Body Tracking SDK on new GPUs | Bundled ONNX Runtime 1.10 (CUDA 11) returns frames without bodies on compute capability >= 10, no error. | `src/onnxruntime_compat.cpp`: a `libonnxruntime.so.1.10.0` stand-in that forwards to ONNX Runtime 1.31.1 (CUDA 12 or 13). |
| Stand-in details | `ReleaseEnv` at exit corrupts the heap with CUDA. TF32 moves joints by up to 35 mm, with TensorRT by up to 96 mm. | The stand-in skips `ReleaseEnv` and sets `NVIDIA_TF32_OVERRIDE=0` (it refuses another value). After any change: `body_tracking` test, joints within 1 mm of the SDK's CPU processing. |
| Stand-in not found | The SDK's own `libonnxruntime.so.1.10.0` from `LD_LIBRARY_PATH` wins. | Link with `-Wl,--disable-new-dtags` (`DT_RPATH`, searched before `LD_LIBRARY_PATH`). The default `RUNPATH` comes after it. |
| CUDA libraries not found | ONNX Runtime's providers fail to load cuBLAS or cudart. | The stand-in's target puts `CUDAToolkit_LIBRARY_DIR` into the executable's `DT_RPATH`; the program links no CUDA library itself. |
| RPATH lost at install | Installed programs do not find CUDA, TensorRT, the SDKs. | `CMAKE_INSTALL_RPATH_USE_LINK_PATH` must stay on. |
| TensorRT engine cache | First start takes about 10 s. Cache 240 MB per GPU architecture in `~/.cache/xblick/k4abt` (`xblick::cache_dir`; `--tensorrt-cache`). | Expected. The program deletes subdirectories `onnxruntime-*_tensorrt-*` of other versions at the start; the setup script's prerequisites in the same directory have other names. |
| Depth engine errors | `libk4a` logs three errors about `libdepthengine.so.2.0` at the start. | The tracker does not use the depth engine. The errors go away with the depth engine in `libk4a1.4/` beside `libk4a` and `libGL`, `libX11` installed. |
| NFS | Do not build or write large outputs on NFS. | Build and write large outputs on a local disk. |

## Layout

| Path | Content |
| --- | --- |
| `src/main_body_tracking.cpp`, `body_tracking.*` | `xblick-k4abt` |
| `src/calibration.*` | the calibration record, reading side (copy of the camera's) |
| `src/session.hpp` | Zenoh session options from strings, for programs that embed the tracker |
| `src/onnxruntime_compat.*` | the ONNX Runtime 1.10 stand-in |
| `tests/`, `tests/data/k4a_test_calibration.json` | the `body_tracking` test |
| `tools/` | `setup.sh`, `check_references.sh` |
| `docs/` | human documentation |

## Defaults

- Base: `cam/azurekinect`. Reads `depth`, `ir`, `k4a_calibration` below it; writes `bodies` and the status
  `body_tracking` on `diagnostics`. Parameters: node `<base>/k4abt`.
- `--processing tensorrt`, `--gpu-device 0`, `--model` from `$K4A_BODY_TRACKING_DNN_MODEL_FILEPATH`, cache
  `xblick::cache_dir("k4abt")`: `~/.cache/xblick/k4abt`, `$XDG_CACHE_HOME/xblick/k4abt`, `$XBLICK_HOME/cache/k4abt`.
- Zenoh: the PC's router at 127.0.0.1:7447, or `--connect`, `--listen`, `--zenoh-config`.
- Standalone start: `source install/env.sh`, `zenohd`, `xblick-azure-kinect` (other repository), then
  `xblick-k4abt`. Look with `xblick rerun <base>/depth --bodies <base>/bodies --spawn` and `xblick diagnostics cam`.
