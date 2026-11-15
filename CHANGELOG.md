# Versions

*The node's [overview](README.md). Versions before 0.3.0 are described in the git history only.*

* **0.3.4** `tools/setup.sh` accepts a CUDA toolkit of version 12 or newer for the node's CUDA parts (nvJPEG, NPP, CUDA
  rings). A version other than the tested 12.8 gives a warning; CUDA 13 is not tested. `--body-tracking` still needs
  CUDA 12, because the pinned ONNX Runtime, cuDNN and TensorRT are builds for it. The package check accepts multiarch
  installs. Compatible with earlier clients: nothing on the wire changed.
* **0.3.3** `tools/setup.sh` sets the node up from scratch on Ubuntu 24.04: it downloads the prerequisites at pinned
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
