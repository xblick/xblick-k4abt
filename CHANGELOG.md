# Versions

*The tracker's [overview](README.md). Up to 0.3.5 the tracker was part of
[xblick-azure-kinect](https://github.com/xblick/xblick-azure-kinect); those entries are that repository's. Versions
before 0.3.0 are described in the git history only.*

* **0.4.0** The tracker has a repository of its own, split out of xblick-azure-kinect 0.3.5 with the history of its
  files. The program `xblick-k4abt`, its topics, options and parameters, the calibration record (version 2) and the
  TensorRT engine cache `~/.cache/xblick/k4abt` are unchanged. It works with camera nodes of xblick-azure-kinect 0.3
  and 0.4. Compatible with earlier clients: nothing on the wire changed.

  What changes for builds and the setup:

  | What | xblick-azure-kinect 0.3.5 | xblick-k4abt 0.4.0 |
  | --- | --- | --- |
  | Repository | `github.com/xblick/xblick-azure-kinect`, `-DXBLICK_K4A_BODY_TRACKING=ON` | `github.com/xblick/xblick-k4abt`, always built; the option `XBLICK_K4A_BODY_TRACKING` is gone |
  | CMake package and target | `xblick_k4a` 0.3 (with `xblick_k4a_BODY_TRACKING`), `xblick_k4a::xblick_k4abt` | `xblick_k4abt` 0.4, `xblick_k4abt::xblick_k4abt` |
  | Installed headers | `include/xblick_k4a` | `include/xblick_k4abt`: `body_tracking.hpp`, `session.hpp` |
  | CUDA toolkit | through the camera's CUDA parts (`XBLICK_K4A_CUDA`) | `CUDAToolkit_ROOT`, needed with `XBLICK_K4A_ONNXRUNTIME_DIR` |
  | Setup | `tools/setup.sh --body-tracking [--untested-cuda13]` | `tools/setup.sh [--cuda-toolkit DIR]` |
  | Prerequisites of the setup | `~/.cache/xblick/azure-kinect` | `~/.cache/xblick/k4abt`; the downloads of the old directory can be moved to its `downloads/` |
  | Test program | `test_k4a_body_tracking`, with the camera node | `test_k4a_body_tracking`, with a stand-in for the camera node |

  `tools/setup.sh` accepts a CUDA toolkit of version 12 or 13 and takes the pinned builds of ONNX Runtime 1.31.1,
  cuDNN 9.15 and TensorRT 10.14.1.48 for its major version. The option `--untested-cuda13` is removed, without an
  alias: the CUDA 13 builds are taken by themselves. Another major version stops the script. The script no longer
  needs the Ubuntu packages `libegl-dev` and `libgl-dev`; `libgl1` and `libx11-6` stay, for the depth engine that
  `libk4a` loads at its start. The other CMake options (`XBLICK_K4A_ONNXRUNTIME_DIR`, `XBLICK_K4A_CUDNN_DIR`,
  `XBLICK_K4A_TENSORRT_DIR`, `K4ABT_TEST_MODEL`, `K4ABT_TEST_PROCESSING`) keep their names.

  Migration: body tracking is now a second clone with its own `tools/setup.sh`, and its prerequisites live in
  `~/.cache/xblick/k4abt`. The files that `--body-tracking` downloaded to `~/.cache/xblick/azure-kinect/downloads`
  have the names this script expects: moved or copied to `~/.cache/xblick/k4abt/downloads`, they are not downloaded
  again. The README (section "Moving from xblick-azure-kinect 0.3") has the commands.

* **0.3.5** `tools/setup.sh --body-tracking --untested-cuda13` on a CUDA 13 toolkit takes the builds of ONNX Runtime
  1.31.1, cuDNN 9.15.0.58 and TensorRT 10.14.1.48 for CUDA 13, pinned with sha256. Without the option,
  `--body-tracking` needs a CUDA 12 toolkit. Compatible with earlier clients: nothing on the wire changed.
* **0.3.4** `tools/setup.sh` accepts a CUDA toolkit of version 12 or newer for the node's CUDA parts (nvJPEG, NPP, CUDA
  rings). `--body-tracking` still needs CUDA 12, because the pinned ONNX Runtime, cuDNN and TensorRT are builds for
  it. The package check accepts multiarch installs. Compatible with earlier clients: nothing on the wire changed.
* **0.3.3** `tools/setup.sh` sets the node up from scratch: it downloads the prerequisites at pinned
  versions with sha256 checksums into Durchblick's cache directory (`~/.cache/xblick/azure-kinect`), builds and tests
  the node in `build-setup/` and installs it in `install/` with `install/env.sh`. It uses no `sudo`. It prints the
  `apt` command for missing Ubuntu packages and the commands for the udev rule. Options: `--cuda`, `--no-gpu`,
  `--cuda-toolkit DIR`, `--body-tracking`. The README's install section uses it; docs/build.md lists every download
  with its source and terms and keeps the manual CMake build. New CMake option `XBLICK_K4A_CUDNN_DIR`: cuDNN 9 for
  body tracking through the RPATH, for CUDA toolkits without cuDNN. The `offline` test builds again without
  `XBLICK_K4A_CUDA` (its `color_rect` check was inside the CUDA-only part). The depth engine needs no
  `LD_LIBRARY_PATH` when the Sensor SDK keeps the layout of Microsoft's package.

* **0.3.2** `xblick-k4abt` keeps its TensorRT engine cache in the node's cache directory of Durchblick 0.5.5,
  `~/.cache/xblick/k4abt` (`xblick::cache_dir("k4abt")`; `XDG_CACHE_HOME` moves it to `$XDG_CACHE_HOME/xblick/k4abt`,
  `XBLICK_HOME` to `$XBLICK_HOME/cache/k4abt`), no longer in `~/.cache/xblick-k4abt` or
  `$XDG_CACHE_HOME/xblick-k4abt`. The first start builds the engine again (about 10 s), and the old directory can be
  deleted. `--tensorrt-cache DIR` still sets the directory explicitly. The build needs Durchblick 0.5.5 or a later 0.5
  (`find_package(xblick 0.5.5)`).

* **0.3.1** Two defaults change: the base name and the `source` label.

  The default base name is `cam/azurekinect`, no longer `robot/azurekinect`, so that the default fits any
  camera PC and follows the core's scheme `cam/<device>/<stream>`. All topics move with it (`cam/azurekinect/color`,
  `cam/azurekinect/depth`, ..., `cam/azurekinect/bodies`), and so do the camera node `/cam/azurekinect` with its
  parameters (a params file needs the key `/cam/azurekinect`) and the tracker's node `/cam/azurekinect/k4abt`. A
  script that subscribes to `robot/azurekinect/...` needs the new names, or the programs started with the old name:
  `xblick-azure-kinect --base robot/azurekinect` and `xblick-k4abt --base robot/azurekinect`. The tools
  `k4a_depth_to_color_check` and `k4a_depth_to_color_mirror` follow (`--base` default `cam/azurekinect`, the mirror's
  `--output` default `cam/azurekinect_sdk`).

  The default `source` label of the camera node in every frame's info record is `cam`, no longer `robot`, as in the
  core's `xblick cam` (with `--playback` it stays `recording`). A reader that filters on `source == "robot"` needs the
  new label, or the node started with `--source robot`. Both old values together:
  `xblick-azure-kinect --base robot/azurekinect --source robot`.

* **0.3.0** Builds against Durchblick 0.5, in which every name in code, on the wire and on disk is `xblick`. The
  programs take the same names; only the prose name of the project stays Durchblick. They work only with programs of
  Durchblick 0.5. Stream names, frame ids, options, parameters and the calibration record are unchanged. The TensorRT
  engine cache moved: the first start of `xblick-k4abt` builds the engine again (about 10 s), and the old directory
  can be deleted.

  | What | 0.2.0 | 0.3.0 |
  | --- | --- | --- |
  | Repository | `github.com/xblick/durchblick-azure-kinect` | `github.com/xblick/xblick-azure-kinect` |
  | Programs | `durchblick-azure-kinect`, `durchblick-azure-kinect-parameters`, `durchblick-k4abt` | `xblick-azure-kinect`, `xblick-azure-kinect-parameters`, `xblick-k4abt` |
  | CMake project | `durchblick_azure_kinect` | `xblick_azure_kinect` |
  | CMake package and targets | `durchblick_k4a`, `durchblick_k4a::durchblick_k4a`, `durchblick_k4a::durchblick_k4abt`, `durchblick_k4a::durchblick_k4a_egl` | `xblick_k4a`, `xblick_k4a::xblick_k4a`, `xblick_k4a::xblick_k4abt`, `xblick_k4a::xblick_k4a_egl` |
  | Package variables | `durchblick_k4a_CUDA`, `durchblick_k4a_BODY_TRACKING` | `xblick_k4a_CUDA`, `xblick_k4a_BODY_TRACKING` |
  | Installed files | `libdurchblick_k4a_egl.so`, `include/durchblick_k4a`, `lib/cmake/durchblick_k4a` | `libxblick_k4a_egl.so`, `include/xblick_k4a`, `lib/cmake/xblick_k4a` |
  | CMake options | `DURCHBLICK_K4A_CUDA`, `DURCHBLICK_K4A_BODY_TRACKING`, `DURCHBLICK_K4A_ONNXRUNTIME_DIR`, `DURCHBLICK_K4A_TENSORRT_DIR` | `XBLICK_K4A_CUDA`, `XBLICK_K4A_BODY_TRACKING`, `XBLICK_K4A_ONNXRUNTIME_DIR`, `XBLICK_K4A_TENSORRT_DIR` |
  | Compile definitions | `DURCHBLICK_K4A_CUDA`, `DURCHBLICK_K4A_ONNXRUNTIME_COMPAT` | `XBLICK_K4A_CUDA`, `XBLICK_K4A_ONNXRUNTIME_COMPAT` |
  | Exported function of the ONNX Runtime shim | `durchblick_onnxruntime_tensorrt_cache` | `xblick_onnxruntime_tensorrt_cache` |
  | TensorRT engine cache (default of `--tensorrt-cache`) | `$XDG_CACHE_HOME/durchblick-k4abt`, `~/.cache/durchblick-k4abt` | `$XDG_CACHE_HOME/xblick-k4abt`, `~/.cache/xblick-k4abt` |
  | Core package | `find_package(durchblick 0.4)`, `durchblick::durchblick`, `"durchblick/<header>.hpp"` | `find_package(xblick 0.5)`, `xblick::xblick`, `"xblick/<header>.hpp"` |
  | C++ namespace | `durchblick::k4a_node` | `xblick::k4a_node` |
  | Point cloud node in the tests | `durchblick-points` | `xblick-points` |
