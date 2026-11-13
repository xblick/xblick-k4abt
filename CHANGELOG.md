# Versions

*The node's [overview](README.md). Versions before 0.3.0 are described in the git history only.*

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
