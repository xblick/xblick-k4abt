# xblick-k4abt

[![xblick core](https://img.shields.io/pypi/v/xblick?label=xblick%20core&color=blue)](https://github.com/xblick/xblick)
[![checks](https://github.com/xblick/xblick-k4abt/actions/workflows/checks.yml/badge.svg)](https://github.com/xblick/xblick-k4abt/actions/workflows/checks.yml)
[![licence](https://img.shields.io/badge/licence-Apache--2.0-blue)](LICENSE)
[![platform](https://img.shields.io/badge/platform-Linux%20x86__64-lightgrey)](README.md#requirements)

`xblick-k4abt` reads the depth and IR streams of an
[xblick-azure-kinect](https://github.com/xblick/xblick-azure-kinect) node and publishes the bodies that the Azure
Kinect Body Tracking SDK finds in them, as [Durchblick](https://github.com/xblick/xblick) samples.

The tracker is a process of its own. It runs on the camera's PC, where it reads the frames from shared memory, or on
any other PC with a GPU, where it receives them through the Zenoh routers. It opens no device, so the camera node and
any number of trackers can run at once.

## What it publishes

The tracker uses the base name of the camera node. The default base is `cam/azurekinect`.

| Topic | Type | Content |
| --- | --- | --- |
| `<base>/bodies` | JSON | The tracked bodies of one depth frame: the SDK's 32 joints, positions in mm in the depth camera's frame. |
| `<base>/diagnostics` | `diagnostic_msgs/DiagnosticArray` | The status `body_tracking`, beside the camera's: rates, latency, settings, about once a second. |

It reads `<base>/depth`, `<base>/ir` and the calibration record on the queryable `<base>/k4a_calibration`.
[docs/design.md](docs/design.md#results) describes the JSON of a result and its stamp.

## Requirements

| Need | For |
| --- | --- |
| Linux on x86-64 | everything |
| CMake 3.24 or newer, Ninja, a C++20 compiler | the build |
| Durchblick 0.5.5 or a later 0.5, C++ package | everything |
| zenoh-c and zenoh-cpp 1.10 | everything |
| Azure Kinect Sensor SDK 1.4 (`libk4a`) with the depth engine; the tracker never calls the depth engine, `libk4a` looks for it when it loads and otherwise logs error lines | the SDK's types and calibration functions |
| Azure Kinect Body Tracking SDK 1.1 with its model `dnn_model_2_0_op11.onnx` | everything |
| nlohmann_json, libjpeg or libjpeg-turbo, libpng | everything |
| NVIDIA GPU and driver | GPU processing, the default |
| CUDA toolkit 12 or 13 | ONNX Runtime's CUDA libraries |
| ONNX Runtime 1.31 and cuDNN 9, builds for the toolkit's CUDA version | the tracker's network |
| TensorRT 10, a build for the toolkit's CUDA version | TensorRT mode, which is the default |

A running `xblick-azure-kinect` 0.3 or newer provides the streams. Every program connects to the Zenoh router of its
PC (`zenohd` on port 7447). Durchblick's
[sessions page](https://github.com/xblick/xblick/blob/main/docs/concepts/sessions.md) explains the routers.

## Install

`tools/setup.sh` installs the prerequisites, builds the tracker, runs its test and installs the tracker in `install/`.
It does not use `sudo`.

```bash
git clone https://github.com/xblick/xblick-k4abt.git
cd xblick-k4abt
tools/setup.sh
```

- If Ubuntu packages are missing, the script stops and prints the `sudo apt install` command for them.
- It needs a CUDA toolkit, 12 or 13, at `/usr/local/cuda` or at `--cuda-toolkit DIR`. It takes the builds of ONNX
  Runtime, cuDNN and TensorRT for the toolkit's major version. Another major version stops the script, because no
  builds are pinned for it.
- It downloads about 11 GB with the CUDA 12 builds. Its test runs the tracker with TensorRT on the GPU.
- A second call does only what changed. `tools/setup.sh --help` lists the options.

The script keeps the prerequisites in Durchblick's cache directory, by default `~/.cache/xblick/k4abt`. Every
download has a pinned version and a sha256 checksum. Microsoft's Sensor SDK and Body Tracking SDK come from
Microsoft's package repository, and NVIDIA's cuDNN and TensorRT from NVIDIA's download server. These binaries are
under their makers' licence terms. The script prints the name of the terms and their link when it starts such a
download.
[docs/build.md](docs/build.md) lists every download with its source and terms, the manual build with CMake and what
the test checks.

### Moving from xblick-azure-kinect 0.3

Up to 0.3.5, `tools/setup.sh --body-tracking` of xblick-azure-kinect built the tracker. Since 0.4.0 body tracking is
this second clone with its own `tools/setup.sh`, and its prerequisites live in `~/.cache/xblick/k4abt`. The files
that the old script downloaded have the names that this script expects, so they need not be downloaded again. With
the default cache directories, move the tracker's files and copy the ones that the camera still needs, then run the
setup:

```bash
mkdir -p ~/.cache/xblick/k4abt/downloads
mv ~/.cache/xblick/azure-kinect/downloads/{libk4abt1.1*,onnxruntime-linux-x64-gpu_*,cudnn-linux-x86_64-*,TensorRT-*} \
   ~/.cache/xblick/k4abt/downloads/
cp ~/.cache/xblick/azure-kinect/downloads/{libk4a1.4*,zenoh*,nlohmann-json-*} ~/.cache/xblick/k4abt/downloads/
tools/setup.sh
```

The script checks the sha256 of every file there as for its own downloads. The unpacked directories in the old cache
(`k4abt-1.1.2`, `onnxruntime-*`, `cudnn-*`, `TensorRT-*`, `cuda13/`) are no longer used and can be deleted.

## Run

In each new shell, put the programs on `PATH` first. Start one Zenoh router per PC, unless one runs already. Start
the camera node of [xblick-azure-kinect](https://github.com/xblick/xblick-azure-kinect), then the tracker. It prints
`ready` when it tracks.

```bash
source install/env.sh
zenohd --no-multicast-scouting -l tcp/0.0.0.0:7447 &
# install/env.sh names the SDK's model; elsewhere add --model dnn_model_2_0_op11.onnx
xblick-k4abt
# a viewer on another host
xblick rerun cam/azurekinect/depth --bodies cam/azurekinect/bodies --spawn --connect <camera host>
```

Most users change these options:

| Option | Default | Effect |
| --- | --- | --- |
| `--base NAME` | `cam/azurekinect` | The base name of the camera node. |
| `--processing tensorrt\|cuda\|cpu` | `tensorrt` | The processing mode of the SDK. |
| `--gpu-device N` | `0` | The GPU. |
| `--mask-min-x PX`, `--mask-max-x PX`, `--mask-max-z MM` | off | The region in which the tracker looks for bodies. |
| `--max-rate HZ` | `0`, every frame | Track at most this many frames per second. |

`--help` lists every option, and the program stops at an unknown option. Every option is also a parameter of the
node `<base>/k4abt`, where the masks and `max_rate` change while it runs. Durchblick's terminal interface (`xblick`,
tab Parameters) shows them, and `--param NAME:=VALUE` or `--params-file FILE` set them at the start.
[docs/options.md](docs/options.md) lists every option.

## Why it is fast

| Change | Effect |
| --- | --- |
| The tracker runs on a current ONNX Runtime with TensorRT. | The SDK needs 7.0 ms per frame, against 16.4 ms with CUDA. |
| TensorRT keeps its engine in a cache. | The tracker starts in 1.6 s instead of 10 s. |
| A separate thread takes the results. | A result goes out 9 to 12 ms after the capture. |

[docs/performance.md](docs/performance.md) has every measurement with its setup.

## Limits

- The SDK's own ONNX Runtime 1.10 finds no bodies on GPUs of compute capability 10 and newer, such as Blackwell. A
  build without `XBLICK_K4A_ONNXRUNTIME_DIR` therefore refuses these GPUs. `tools/setup.sh` always builds with a
  current ONNX Runtime.
- The calibration record has version 2. The tracker refuses camera nodes with another version.

## Documentation

| Page | Content |
| --- | --- |
| [docs/options.md](docs/options.md) | Every command-line option and parameter. |
| [docs/performance.md](docs/performance.md) | Body tracking times. |
| [docs/design.md](docs/design.md) | How the tracker works inside, the decisions and the special cases of the Body Tracking SDK. |
| [docs/build.md](docs/build.md) | Dependencies, the setup script, CMake options, the test. |

The camera node's documentation describes the streams that the tracker reads:
[xblick-azure-kinect](https://github.com/xblick/xblick-azure-kinect).

## Licence and credits

The code of this repository is under the Apache License 2.0 ([LICENSE](LICENSE)). The test calibration
`tests/data/k4a_test_calibration.json` comes from Microsoft's Azure Kinect Sensor SDK, under the MIT licence. The
repository does not contain the Sensor SDK, its closed-source depth engine or the Body Tracking SDK. You install them
separately, and they have Microsoft's licence terms. [NOTICE](NOTICE) has the details.
