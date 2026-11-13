# Performance of xblick-azure-kinect

This page has every measurement of the node with its setup: the cost per colour frame, the cost per capture with
subscribers, the camera on a busy GPU, and the body tracking. The README has the short table.

## Cost per colour frame

Measured on 2026-10-27 with a benchmark program that is not part of this repository, on an AMD EPYC 9335 with an RTX
PRO 6000. The input is JPEG frames of a camera image at each colour resolution, with quality 90 and 4:2:2
subsampling. We did not measure the MJPEG quality of the camera itself.

| Mode | JPEG only | CPU `rgb8` | nvJPEG |
| --- | --- | --- | --- |
| 720P | 0.001 ms | 2.02 ms | 0.56 ms |
| 1080P | 0.006 ms | 4.39 ms | 1.20 ms |
| 1440P | 0.008 ms | 7.83 ms | 2.09 ms |
| 2160P | 0.024 ms | 17.44 ms | 4.67 ms |
| 3072P | 0.028 ms | 25.43 ms | 6.94 ms |

A subscriber that takes JPEG costs the node almost nothing. Examples are a remote viewer and a teleoperation client. A
subscriber that takes pixels costs one decode. With `--cuda-device` the node decodes on the GPU straight into the
CUDA ring.

Depth and IR go from the SDK's memory into the ring with one copy each, which is 0.7 MB per frame.

## Cost per capture with subscribers

Measured on 2026-10-27 with `k4a_node_bench <frames> <calibration> [captures] [cuda device|none]` on an RTX PRO 6000,
with a development version of Durchblick. The tool exists in CUDA builds. It measures `Node::publish` per
capture in NFOV unbinned at 30 captures per second, over 150 captures.

All four streams have subscribers: colour, depth, IR and `depth_to_color`. Each subscriber is a separate process that
takes every frame as a view into the ring. Every subscriber received 149 or 150 of the 150 frames. The first frame
can fall on the start of the count.

| Colour | Subscribers | Publish, median (p90) | Colour | Depth + IR | `depth_to_color` |
| --- | --- | --- | --- | --- | --- |
| 720P | shm | 1.6 (1.7) ms | 0.94 ms | 0.10 ms | 0.55 ms |
| 720P | CUDA | 1.1 (1.2) ms | 0.24 ms | 0.17 ms | 0.70 ms |
| 720P | shm + CUDA | 2.0 (2.0) ms | 0.94 ms | 0.26 ms | 0.77 ms |
| 2160P | shm | 9.2 (9.9) ms | 6.1 ms | 0.12 ms | 3.1 ms |
| 2160P | CUDA | 4.0 (4.5) ms | 0.86 ms | 0.18 ms | 3.1 ms |
| 2160P | shm + CUDA | 10.5 (11.3) ms | 6.1 ms | 0.30 ms | 4.2 ms |
| 3072P | shm | 13.8 (14.1) ms | 8.9 ms | 0.11 ms | 4.9 ms |
| 3072P | CUDA | 6.7 (7.0) ms | 1.0 ms | 0.18 ms | 5.6 ms |
| 3072P | shm + CUDA | 16.1 (16.8) ms | 8.9 ms | 0.29 ms | 7.1 ms |
| 720P | shm, `--cuda-device none` | 7.7 (8.0) ms | 6.2 ms | 0.11 ms | 1.2 to 1.3 ms |
| 2160P | shm, `--cuda-device none` | 26 to 27 (27 to 28) ms | 19 to 20 ms | 0.1 ms | 5.9 to 6.3 ms |
| 3072P | shm, `--cuda-device none` | 32 to 39 (34 to 40) ms | 28 ms | 0.1 ms | 4.8 to 11 ms |

Without subscribers a capture costs 0.02 ms. In this measurement all work happened in the capture loop, one capture
after the other, and the SDK's transformation computed `depth_to_color`. Both have changed since. The section
"The camera on a busy GPU" describes the changes.

What the columns contain:

- **Colour for CUDA subscribers.** nvJPEG decodes into GPU memory, and the node copies the frame within the GPU into
  the ring. `publish` does not wait for the GPU. The frame is ready for the subscriber when the decoding has finished.
- **Colour for shm subscribers.** The node downloads the frame straight into the slot of the host ring, which
  Durchblick page-locks. For a 4K RGB frame the download takes 0.7 ms, against 2.6 ms without page-locking. `publish`
  waits for the decoding, which is most of the colour time.
- **`--cuda-device none`.** libjpeg decodes on the CPU, which takes three to five times as long. For this reason
  `--cuda-device 0` is the default in CUDA builds.
- **`depth_to_color`.** The depth engine computes it on the GPU, and the SDK returns it in host memory. For CUDA
  subscribers the node uploads it again, because the SDK's transformation has no GPU output.
- **Depth and IR.** The node makes one copy into each ring.

## Rectified colour

Measured on 2026-10-28 in the `offline` test at 720P over 60 frames, on an AMD EPYC 9335 with an RTX PRO 6000.

| Step | CPU | GPU (NPP) |
| --- | --- | --- |
| Remap and publish `color_rect` | 3.2-3.7 ms | 0.6-0.7 ms |
| The whole colour step with decoding | 4.7-5.2 ms | 1.9 ms |

## The camera on a busy GPU

Observed on 2026-11-10 on a camera PC where Durchblick's nodes share the GPU under MPS. The camera reached
16-19 frames per second where it should deliver 30. Two changes brought it back to 30.

| What waited | Cost before | Change |
| --- | --- | --- |
| The SDK's transformation is OpenGL work in the depth engine. It waited for its GPU time slices. | 15-18 ms per frame | The node computes `depth_to_color` on the CPU. `--sdk-depth-to-color` selects the SDK's transformation. |
| A publish into a CUDA ring uploads the frame. Each upload waited for the GPU. | 14-17 ms per stream, about 50 ms per capture for all streams in sequence | Four threads publish the streams in parallel. `--sync-publish` selects the capture loop. |

[design.md](design.md) describes both mechanisms.

## Body tracking

Measured on an RTX PRO 6000 with a rendered mannequin (`tests/test_body_tracking.cpp`).

| Mode | Time per frame in the SDK | Of it the network | Capture to published result | Start |
| --- | --- | --- | --- | --- |
| TensorRT 10.14.1 | 7.0 ms | 3.3 ms | 9 to 12 ms | 10 s at the first start, then 1.6 s |
| CUDA | 16.4 ms | 9.2 ms | 14 to 17 ms | 1.4 s |
| CPU | 123 ms | 98 ms | not measured | not measured |

The time per frame in the SDK runs from enqueue to result and is the median of 190 frames. The time from capture to
the published result is the median of 20 results at 30 frames per second.

The tracker takes results in a separate thread. An earlier version took each result between two frames, so each
result waited for the next frame. That took 33 ms in both modes.

With CUDA and with TensorRT, every joint is within 1 mm of the SDK's own ONNX Runtime on the CPU.

## Status and limits

- All measurements use recordings, synthetic JPEG frames or a rendered mannequin. Only the section "The camera on a
  busy GPU" comes from a real device.
- The table "Cost per capture with subscribers" predates the CPU `depth_to_color` and the publishing threads. Nobody
  has repeated it since.
