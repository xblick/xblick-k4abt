# Options of the programs

This page lists every command-line option of `xblick-azure-kinect`, `xblick-k4abt` and
`xblick-azure-kinect-parameters`. `xblick-azure-kinect` and `xblick-k4abt` list theirs with `--help`
(Durchblick's `xblick::Options`); `xblick-azure-kinect-parameters` does not. The header comments of
`src/main_camera.cpp`, `src/main_body_tracking.cpp` and `src/main_parameters.cpp` are the source of this page. Every
program stops with an error at an unknown option.

## Zenoh session, all programs

Each program connects to the Zenoh router of its PC at `127.0.0.1:7447` by default.

| Option | Effect |
| --- | --- |
| `--connect ENDPOINT...` | Connect to these endpoints. |
| `--listen ENDPOINT...` | Listen on these endpoints. |
| `--zenoh-config FILE` | Use this Zenoh configuration file. |

## xblick-azure-kinect

### Source

| Option | Default | Effect |
| --- | --- | --- |
| `--base NAME` | `cam/azurekinect` | The base name of all topics. |
| `--serial NUMBER` | none | Open the device with this serial number. |
| `--index N` | `0` | Open the device with this index. |
| `--playback FILE` | none | Play a recording. The file is an MKV of `k4arecorder` with MJPEG colour, depth and IR. |
| `--loop` | off | Start the recording again at its end. |
| `--no-realtime` | off | Play the recording without its original pace. |
| `--source NAME` | `cam`, with `--playback`: `recording` | The `source` field in the attachment of every frame. |
| `--status-period SECONDS` | `5` | The period of the status line on the terminal. |

### Parameters

[parameters.md](parameters.md) describes the parameters.

| Option | Effect |
| --- | --- |
| `--params-file FILE` | Read start values from a ROS params YAML. You can repeat the option. |
| `--param NAME:=VALUE...` | Set start values. A later value wins, and these values win over the files. |
| `--dump-params FILE` | Write the values in effect as a params file. |
| `--params-read-only` | Serve the parameters, but refuse every change at run time. |
| `--fps N` | Short for the parameter `fps`. Default 30. |
| `--depth-mode MODE` | Short for `depth.mode`. Default `NFOV_UNBINNED`. |
| `--color-resolution MODE` | Short for `color.resolution`. Default `720P`. |
| `--brightness N` | Short for `color.brightness`. Default 128. |
| `--clock-offset mean\|min` | Short for `clock.offset_estimator`. Default `mean`. |

The five short options refuse the value `auto`. Give the value, or set the `auto_*` parameter of a control with
`--param`, for example `--param color.auto_brightness:=true` where the device has that mode.

### GPU and publishing

| Option | Default | Effect |
| --- | --- | --- |
| `--cuda-device N\|none` | `0` in builds with `XBLICK_K4A_CUDA`, else `none` | The GPU for nvJPEG decoding, NPP remapping and the CUDA rings. `none` turns the GPU path off. |
| `--depth-engine-gpu N` | the value of `--cuda-device`, else `0` | The CUDA device on which the depth engine gets its OpenGL context through EGL. |
| `--sync-publish` | off | Publish all streams in the capture loop, one after the other. By default four threads publish them. |

### Depth

| Option | Default | Effect |
| --- | --- | --- |
| `--no-depth-to-color` | off | Do not offer `depth_to_color` and `depth_to_color_rect`. |
| `--sdk-depth-to-color` | off | Compute `depth_to_color` with the SDK's transformation in the depth engine. By default the node computes it on the CPU. |
| `--no-depth-to-color-interpolation` | off | Give each depth pixel's footprint one depth throughout. By default the depth follows the slope of the surface. |
| `--keep-flying-pixels` | off | Keep the depth pixels in the gap between two surfaces. By default the node leaves them out. |
| `--keep-sparse-pixels` | off | Keep the depth pixels that lie in no complete 2 x 2 square on one surface. By default the node leaves them out. |
| `--max-depth-mm MM` | `0`, no limit | Set depths beyond this distance to 0 in every depth stream. |

[design.md](design.md#depth_to_color-on-the-cpu) explains these options.

### Frames and calibration

| Option | Default | Effect |
| --- | --- | --- |
| `--depth-frame-id NAME` | `AzureKinectDepthCamera` | The frame of depth and IR. |
| `--color-frame-id NAME` | `AzureKinectCamera` | The frame of the colour streams. |
| `--imu-frame-id NAME` | `AzureKinectImu` | The frame of the IMU messages. |
| `--no-imu` | off | Do not publish the IMU. |
| `--color-calibration FILE` | none | A ROS calibration file that replaces the colour intrinsics. |
| `--depth-calibration FILE` | none | A ROS calibration file that replaces the depth intrinsics. |

The calibration files also work with `--playback`. [streams.md](streams.md#calibration-files) states what a file
must contain.

## xblick-k4abt

The tracker runs on the camera's host and reads the host rings there. On another host with a GPU it receives raw
frames through the routers.

| Option | Default | Effect |
| --- | --- | --- |
| `--base NAME` | `cam/azurekinect` | The base name of the camera node. |
| `--model FILE` | `$K4A_BODY_TRACKING_DNN_MODEL_FILEPATH` | The model, for example `dnn_model_2_0_op11.onnx`. |
| `--processing tensorrt\|cuda\|cpu` | `tensorrt` | The processing mode of the SDK. |
| `--gpu-device N` | `0` | The GPU. |
| `--smoothing X` | `0` | The temporal smoothing of the SDK. |
| `--mask-min-x PX`, `--mask-max-x PX` | `-1`, off | The tracker ignores depth and IR outside these image columns. Live: it changes while the node runs. |
| `--mask-max-z MM` | `-1`, off | The tracker ignores depth and IR beyond this distance. Live: it changes while the node runs. |
| `--max-rate HZ` | `0`, every frame | Track at most this many frames per second. Live: it changes while the node runs. |
| `--tensorrt-cache DIR` | `$XDG_CACHE_HOME/xblick-k4abt`, else `~/.cache/xblick-k4abt` | The directory of TensorRT's engine cache. An empty value builds the engine at every start, which takes 10 s. |
| `--status-period SECONDS` | `5` | The period of the status line on the terminal. |

`--tensorrt-cache` exists only in builds with `XBLICK_K4A_ONNXRUNTIME_DIR`, and the program accepts it only with
`--processing tensorrt`. The masks set depth and IR outside them to 65535. A mask value of 0 or below turns that mask
off.

The program finds CUDA 12 and cuDNN 9 through its RPATH. It finds TensorRT there too in builds with
`XBLICK_K4A_TENSORRT_DIR`. In other builds TensorRT 10 must be on `LD_LIBRARY_PATH`.
[design.md](design.md#libraries-at-run-time) explains this.

## xblick-azure-kinect-parameters

This client reads and sets the parameters of a node over its parameter services, like `ros2 param`. It works with any
node that serves them.

| Option | Effect |
| --- | --- |
| `--node BASE` | The node, for example `cam/azurekinect`. |
| `--list` | List the parameter names. |
| `--describe [NAME...]` | Print the descriptors. Without a name it prints all of them. |
| `--get [NAME...]` | Print the values. Without a name it prints all of them. |
| `--set NAME:=VALUE...` | Set values. A value is a YAML scalar such as `true`, `15625`, `0.5` or `NFOV_UNBINNED`. |
| `--atomically` | With `--set`: set all values or none. |

The client exits with status 1 if a set fails.

## Parameters

Every option is also a parameter of the node `<base>` for the camera and `<base>/k4abt` for the body tracker. The live
ones change while the node runs: `mask_min_x`, `mask_max_x`, `mask_max_z`, `max_rate`; the tables mark them. The
others are read-only and apply at the next start, where `--param NAME:=VALUE` or `--params-file FILE` set them. The
camera's settings that change while it runs are its own parameters ([parameters.md](parameters.md)); its options are
read-only. Durchblick's terminal interface (`xblick`, tab Parameters) and `xblick param` show them.
