# AGENTS.md

Azure Kinect as Durchblick streams: `durchblick-azure-kinect` (camera or MKV playback) and `durchblick-k4abt` (body
tracking, separate process, reads those streams). C++20, plain CMake project `durchblick_azure_kinect`, installs the
CMake package `durchblick_k4a` for programs that build on the node and the tracker. No point clouds here: Durchblick's
generic node computes them. Human documentation: `README.md`, then `docs/` (streams, options, parameters, timestamps,
performance, design, build, hardware-checks). Transport-wide traps (Zenoh sessions and routers, CUDA rings, hanging
tests): `AGENTS.md` of the core repository (github.com/xblick/durchblick). Style of all documentation: the core's
`docs/project/writing-docs.md`.

## Commands

```bash
# Build on a local disk, never on a shared filesystem. <...> = install prefixes. Full option list: docs/build.md.
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="<durchblick>;<zenoh>;<k4a>;<nlohmann_json>" \
  -DDURCHBLICK_K4A_CUDA=ON -DCUDAToolkit_ROOT=<cuda-12.8> \
  [-DDURCHBLICK_K4A_BODY_TRACKING=ON -Dk4abt_DIR=... -DDURCHBLICK_K4A_ONNXRUNTIME_DIR=... \
   -DDURCHBLICK_K4A_TENSORRT_DIR=... -DK4ABT_TEST_MODEL=<sdk>/usr/bin/dnn_model_2_0_op11.onnx]
cmake --build build
ctest --test-dir build --output-on-failure   # LD_LIBRARY_PATH: k4a + depth engine, zenoh, k4abt
cmake --install build --prefix <durchblick_k4a>   # the CMake package durchblick_k4a (durchblick_k4a_DIR)
tools/check_references.sh                         # the reference check of the CI
```

Durations: not recorded. Give `ctest` a timeout.

- Tests need no camera: `offline`, `parameters`, `body_tracking`. Content per test: docs/build.md.
- `body_tracking` needs the SDK's full model `dnn_model_2_0_op11.onnx`. The lite model finds nothing.
  `-DK4ABT_TEST_PROCESSING=cpu|cuda|tensorrt`.
- Benchmark: `tools/k4a_node_bench.cpp` (CUDA builds).

## Rules

- Fail fast. A wrong calibration file, an unknown option, a missing depth engine or an unsupported GPU stops the
  program with the cause.
- The programs define their options with Durchblick's `durchblick::Options` (`--help` lists them), and every option is
  also a parameter of the node. Keep the header comments of `src/main_camera.cpp` and `src/main_body_tracking.cpp`
  and docs/options.md in sync with them.
- Read docs/design.md before you change build or run-time details. Each decision has a reason there (EGL depth engine,
  ONNX Runtime stand-in, RPATH, diagnostics). Keep that table current. Do not undo a decision without reading its
  reason.
- Run-time libraries come from RPATH. Never put them on a workspace-wide `LD_LIBRARY_PATH`: that would shadow, for
  example, PyTorch's cuDNN for every program of the workspace. Only exception: the depth engine, which `libk4a` opens
  by name.
- A capture or tracking failure ends the program.
- The calibration record has version 2. Camera nodes and trackers of another version refuse each other.
- Documentation: Markdown only in `README.md` and `docs/`, one purpose per page, history in commit messages.

## Traps

| Trap | Symptom | Fix |
| --- | --- | --- |
| Depth engine without X | SDK error 204. Without the plugin the SDK's transformation returns a null handle, no error. | `libdurchblick_k4a_egl.so` (`src/egl_glx.cpp`) provides the 15 Xlib/GLX functions with EGL on the NVIDIA GPU. The node checks the handle. |
| Body Tracking SDK on new GPUs | Bundled ONNX Runtime 1.10 (CUDA 11) returns frames without bodies on compute capability >= 10, no error. | `src/onnxruntime_compat.cpp`: a `libonnxruntime.so.1.10.0` stand-in that forwards to ONNX Runtime 1.31.1 (CUDA 12). |
| Stand-in details | `ReleaseEnv` at exit corrupts the heap with CUDA. TF32 moves joints by up to 35 mm, with TensorRT by up to 96 mm. | The stand-in skips `ReleaseEnv` and sets `NVIDIA_TF32_OVERRIDE=0` (it refuses another value). After any change: `body_tracking` test, joints within 1 mm of the SDK's CPU processing. |
| Stand-in not found | The SDK's own `libonnxruntime.so.1.10.0` from `LD_LIBRARY_PATH` wins. | Link with `-Wl,--disable-new-dtags` (`DT_RPATH`, searched before `LD_LIBRARY_PATH`). The default `RUNPATH` comes after it. |
| RPATH lost at install | Installed programs do not find CUDA, TensorRT, the SDKs. | `CMAKE_INSTALL_RPATH_USE_LINK_PATH` must stay on. |
| TensorRT engine cache | First start takes about 10 s. Cache 240 MB per GPU architecture in `~/.cache/durchblick-k4abt` (`--tensorrt-cache`). | Expected. The program deletes subdirectories of other ONNX Runtime/TensorRT versions at the start. |
| MJPEG without Huffman tables | nvJPEG and other decoders fail. | The node inserts libjpeg's standard tables. Keep that step. |
| Body tracking build switch | Body tracking is built when `k4abt_DIR` is in the environment. With `k4abt_DIR` from elsewhere and no ONNX Runtime directory the build stops. | Set `DURCHBLICK_K4A_ONNXRUNTIME_DIR`, or `-DDURCHBLICK_K4A_BODY_TRACKING=OFF`. |
| Two programs on one device | Only one program can open a device. | No other driver may open the same camera. |
| CUDA ring in its own process | The process that created a CUDA ring cannot open it. | The `offline` test reads it from `test_k4a_cuda_reader`, a separate process. |
| NFS | Do not build or write large outputs on NFS. | Build and write large outputs on a local disk. |

## Layout

| Path | Content |
| --- | --- |
| `src/main_camera.cpp`, `node.*`, `source.*` | camera/playback program, publishing, capture loop |
| `src/settings.*`, `main_parameters.cpp` | parameters, the parameter client |
| `src/calibration.*`, `clock.*`, `rectify.*` | calibration files, device clock, `color_rect` |
| `src/depth_to_color.*` | `depth_to_color(_rect)` on the CPU (SDK's transformation only with `--sdk-depth-to-color`) |
| `src/jpeg.*`, `nvjpeg_decoder.*` | MJPEG handling, nvJPEG decoding |
| `src/egl_glx.cpp` | EGL stand-in for the depth engine's Xlib/GLX |
| `src/main_body_tracking.cpp`, `body_tracking.*` | `durchblick-k4abt` |
| `src/onnxruntime_compat.*` | the ONNX Runtime 1.10 stand-in |
| `tests/`, `tests/data/k4a_test_calibration.json` | `offline`, `parameters`, `body_tracking` tests |
| `tools/` | benchmark, `depth_to_color` check and mirror, `check_references.sh` |
| `docs/` | human documentation |

## Defaults

- Base: `robot/azurekinect`. Topics below it: `color`, `color_rect`, `depth`, `ir`, `depth_to_color`,
  `depth_to_color_rect`, `imu`, `tf_static`, `k4a_calibration`, `diagnostics`, `bodies`. `points` comes from
  `durchblick-points`.
- Mode: `fps` 30, `depth.mode` NFOV_UNBINNED, `color.resolution` 720P, `color.brightness` 128,
  `clock.offset_estimator` mean. Ring slots per stream: 32.
- `--cuda-device`: 0 in CUDA builds, else none. `--depth-engine-gpu`: the CUDA device, else 0.
- Frames: `AzureKinectDepthCamera`, `AzureKinectCamera`, `AzureKinectImu`. `source`: `robot`, with playback
  `recording`.
- Tracker: `--processing tensorrt`, `--model` from `$K4A_BODY_TRACKING_DNN_MODEL_FILEPATH`, cache
  `$XDG_CACHE_HOME/durchblick-k4abt` or `~/.cache/durchblick-k4abt`.
- Zenoh: the PC's router at 127.0.0.1:7447, or `--connect`, `--listen`, `--zenoh-config`.
- Standalone start: `zenohd`, then `durchblick-azure-kinect [--cuda-device 0]` (or `--playback take.mkv --loop`),
  `durchblick-k4abt --model dnn_model_2_0_op11.onnx`, `durchblick-points --depth <base>/depth --color <base>/color`.
  Look with `blick rerun ... --spawn` and `blick diagnostics robot`.
- Tested mostly offline (recordings the SDK wrote, a rendered mannequin). Behaviour with a real device is largely
  unverified: docs/hardware-checks.md.
