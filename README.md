# xblick-azure-kinect

[![xblick core](https://img.shields.io/pypi/v/xblick?label=xblick%20core&color=blue)](https://github.com/xblick/xblick)
[![checks](https://github.com/xblick/xblick-azure-kinect/actions/workflows/checks.yml/badge.svg)](https://github.com/xblick/xblick-azure-kinect/actions/workflows/checks.yml)
[![licence](https://img.shields.io/badge/licence-Apache--2.0-blue)](LICENSE)
[![platform](https://img.shields.io/badge/platform-Linux%20x86__64-lightgrey)](README.md#requirements)

`xblick-azure-kinect` publishes the colour, depth, IR and IMU data of an Azure Kinect as
[Durchblick](https://github.com/xblick/xblick) streams, at up to 30 captures per second.
`xblick-k4abt` is a second program that reads those streams and publishes the bodies that the Azure Kinect Body
Tracking SDK finds.

The node produces each representation of a stream only while a subscriber asks for it. It needs no X server, so it
runs on a headless camera PC.

## What it publishes

The node publishes below one base name. The default base is `cam/azurekinect`.

| Topic | Type | Content |
| --- | --- | --- |
| `<base>/color` | image, `rgb8` | The colour camera. JPEG subscribers get the camera's own JPEG bytes. |
| `<base>/color_rect` | image, `rgb8` | The colour image without lens distortion. |
| `<base>/depth` | image, `16UC1`, mm | The depth camera in its own geometry (640 x 576 in NFOV unbinned). |
| `<base>/ir` | image, `mono16` | The IR image of the depth camera, with the stamp of the depth frame. |
| `<base>/depth_to_color` | image, `16UC1`, mm | The depth as the colour camera sees it. |
| `<base>/depth_to_color_rect` | image, `16UC1`, mm | The depth as the camera of `color_rect` sees it. |
| `<base>/imu` | `sensor_msgs/Imu` | One message per IMU sample, about 1600 per second. |
| `<base>/tf_static` | `tf2_msgs/TFMessage` | The poses of the colour camera and the IMU in the depth camera's frame. |
| `<base>/k4a_calibration` | JSON, queryable | The calibration record of the device. |
| `<base>/diagnostics` | `diagnostic_msgs/DiagnosticArray` | Rates, drops, restarts, temperature and settings, about once a second. |
| `<base>/bodies` | JSON | The tracked bodies, published by `xblick-k4abt`. |

Every image stream carries its `CameraInfo`. Durchblick's point cloud node computes coloured point clouds from these
streams on `<base>/points`. [docs/streams.md](docs/streams.md) describes every stream, the calibration files and the
point clouds.

## Requirements

| Need | For |
| --- | --- |
| Durchblick 0.5.5 or a later 0.5, C++ package | everything |
| zenoh-c and zenoh-cpp 1.10 | everything |
| Azure Kinect Sensor SDK with `k4arecord` and the depth engine | everything |
| nlohmann_json, libjpeg or libjpeg-turbo | everything |
| NVIDIA driver with EGL, GLVND's `libGL` and `libX11` | the depth engine without an X server |
| CUDA 12 toolkit with nvJPEG | optional: colour decoding on the GPU, CUDA rings |
| Azure Kinect Body Tracking SDK 1.1 | optional: `xblick-k4abt` |
| ONNX Runtime for CUDA 12 (tested: 1.31.1) | body tracking on GPUs of compute capability 10 and newer |
| TensorRT 10 (tested: 10.14.1) | body tracking in TensorRT mode, which is the default |

Every program connects to the Zenoh router of its PC (`zenohd` on port 7447). Durchblick's
[sessions page](https://github.com/xblick/xblick/blob/main/docs/concepts/sessions.md) explains
the routers.

## Install

1. Build and test the project. Replace each `<...>` with the install prefix of that package.

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="<xblick>;<zenoh>;<k4a>;<nlohmann_json>" \
     -DXBLICK_K4A_CUDA=ON -DCUDAToolkit_ROOT=<cuda-12.8>
   cmake --build build
   ctest --test-dir build
   ```

2. Install it, if another project must find it as the CMake package `xblick_k4a`.

   ```bash
   cmake --install build --prefix <xblick_k4a>
   ```

The tests need no camera. [docs/build.md](docs/build.md) has the options for body tracking, the libraries that the
tests need on `LD_LIBRARY_PATH`, and what each test checks.

## Run

Start the camera node. It prints `ready` when the streams are up.

```bash
xblick-azure-kinect
```

The other programs read the streams of that node.

```bash
# a recording of k4arecorder in place of the camera
xblick-azure-kinect --playback take.mkv --loop
# body tracking
xblick-k4abt --model dnn_model_2_0_op11.onnx
# coloured point clouds on cam/azurekinect/points, with Durchblick's point cloud node
xblick-points --depth cam/azurekinect/depth --color cam/azurekinect/color
# a viewer on another host
xblick rerun cam/azurekinect/color cam/azurekinect/depth cam/azurekinect/ir \
  cam/azurekinect/depth_to_color --points cam/azurekinect/points --spawn --connect <camera host>
```

Most users change these options:

| Option | Default | Effect |
| --- | --- | --- |
| `--base NAME` | `cam/azurekinect` | The base name of all topics. |
| `--cuda-device N\|none` | `0` in CUDA builds | The GPU that decodes the colour image and holds the CUDA rings. |
| `--color-resolution MODE` | `720P` | The colour resolution. |
| `--depth-mode MODE` | `NFOV_UNBINNED` | The depth mode. |
| `--params-file FILE`, `--param NAME:=VALUE` | none | Start values of the parameters. |
| `--color-calibration FILE`, `--depth-calibration FILE` | none | Calibration files that replace the device's intrinsics. |

`--help` lists every option of a program, and a program stops at an unknown option. Every option is also a
parameter: of the node `<base>` for the camera, read-only, and of `<base>/k4abt` for the body tracker, where the masks
and `max_rate` change while it runs. Durchblick's terminal interface (`xblick`, tab Parameters) shows them, and
`--param NAME:=VALUE` or `--params-file FILE` set them at the start.
[docs/options.md](docs/options.md) lists every option. [docs/parameters.md](docs/parameters.md) lists the camera
settings that you can change while the node runs, such as exposure and white balance.

## Why it is fast

At 1080P, decoding a colour frame on the CPU takes 4.39 ms. This node spends 0.006 ms per frame for a JPEG
subscriber and 1.20 ms for a subscriber that reads pixels on the GPU.

| Change | Effect |
| --- | --- |
| The node passes the camera's MJPEG bytes on unchanged. | A JPEG subscriber costs no decoding. |
| nvJPEG decodes the colour image into GPU memory. | 1080P pixels cost 1.20 ms, against 4.39 ms on the CPU. |
| The node computes `depth_to_color` on the CPU. | The camera keeps 30 captures per second on a GPU that other programs use. |
| Four threads publish the streams in parallel. | A slow upload of one stream does not delay the next capture. |
| Body tracking uses TensorRT. | The SDK needs 7.0 ms per frame, against 16.4 ms with CUDA. |

[docs/performance.md](docs/performance.md) has every measurement with its setup.

## Limits

- We tested the programs offline, with recordings that the SDK wrote and with a rendered mannequin. Most behaviour
  with a real device is unverified.
- Nobody has measured the fixed delay between the exposure and the arrival of an image. Stamps are late by that delay.
- The calibration files replace the intrinsics only. The extrinsics stay the factory values.
- One program can open a device. No other driver may open the same camera.

[docs/hardware-checks.md](docs/hardware-checks.md) is the checklist for a session with the camera attached.

## Documentation

| Page | Content |
| --- | --- |
| [docs/streams.md](docs/streams.md) | Every stream in detail, calibration files, rectified colour, point clouds, viewing. |
| [docs/options.md](docs/options.md) | Every command-line option of the three programs. |
| [docs/parameters.md](docs/parameters.md) | The camera's parameters, the parameter services, the client, the diagnostics. |
| [docs/timestamps.md](docs/timestamps.md) | How the node computes the stamp of a frame. |
| [docs/performance.md](docs/performance.md) | Cost per frame and per capture, body tracking times. |
| [docs/design.md](docs/design.md) | How the node works inside, the decisions and the special cases of the Azure Kinect. |
| [docs/build.md](docs/build.md) | Dependencies, CMake options, tests. |
| [docs/hardware-checks.md](docs/hardware-checks.md) | The checks that still need a camera. |

Durchblick's concept pages explain the terms that these pages use:
[streams and topics](https://github.com/xblick/xblick/blob/main/docs/concepts/streams-and-topics.md),
[transports](https://github.com/xblick/xblick/blob/main/docs/concepts/transports.md),
[rings](https://github.com/xblick/xblick/blob/main/docs/concepts/rings.md) and
[timestamps](https://github.com/xblick/xblick/blob/main/docs/concepts/timestamps.md).

## Licence and credits

The code of this repository is under the Apache License 2.0 ([LICENSE](LICENSE)). The test calibration
`tests/data/k4a_test_calibration.json` comes from Microsoft's Azure Kinect Sensor SDK, under the MIT licence. The
repository does not contain the Sensor SDK, its closed-source depth engine or the Body Tracking SDK. You install them
separately, and they have Microsoft's licence terms. [NOTICE](NOTICE) has the details.
