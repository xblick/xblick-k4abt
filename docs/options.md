# Options of xblick-k4abt

This page lists every command-line option of `xblick-k4abt`. The program lists them with `--help` (Durchblick's
`xblick::Options`). The header comment of `src/main_body_tracking.cpp` is the source of this page. The program stops
with an error at an unknown option.

## Zenoh session

The program connects to the Zenoh router of its PC at `127.0.0.1:7447` by default.

| Option | Effect |
| --- | --- |
| `--connect ENDPOINT...` | Connect to these endpoints. |
| `--listen ENDPOINT...` | Listen on these endpoints. |
| `--zenoh-config FILE` | Use this Zenoh configuration file. |

## Tracking

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
| `--tensorrt-cache DIR` | `~/.cache/xblick/k4abt`, moved by `XBLICK_HOME` and `XDG_CACHE_HOME` | The directory of TensorRT's engine cache. An empty value builds the engine at every start, which takes 10 s. |
| `--status-period SECONDS` | `5` | The period of the status line on the terminal. |

`--tensorrt-cache` exists only in builds with `XBLICK_K4A_ONNXRUNTIME_DIR`, and the program accepts it only with
`--processing tensorrt`. The masks set depth and IR outside them to 65535. A mask value of 0 or below turns that mask
off.

The program finds the CUDA toolkit's libraries and cuDNN 9 through its RPATH. It finds TensorRT there too in builds
with `XBLICK_K4A_TENSORRT_DIR`. In other builds TensorRT 10 must be on `LD_LIBRARY_PATH`.
[design.md](design.md#libraries-at-run-time) explains this.

## Parameters

| Option | Effect |
| --- | --- |
| `--params-file FILE` | Read start values from a ROS params YAML. You can repeat the option. |
| `--param NAME:=VALUE...` | Set start values. A later value wins, and these values win over the files. |
| `--dump-params FILE` | Write the values in effect as a params file. |
| `--params-read-only` | Serve the parameters, but refuse every change at run time. |

Every option is also a parameter of the node `<base>/k4abt`. The live ones change while the node runs: `mask_min_x`,
`mask_max_x`, `mask_max_z`, `max_rate`; the table marks them. The others are read-only and apply at the next start,
where `--param NAME:=VALUE` or `--params-file FILE` set them. Durchblick's terminal interface (`xblick`, tab
Parameters) and `xblick param` show them.
