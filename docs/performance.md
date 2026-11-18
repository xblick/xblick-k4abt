# Performance of xblick-k4abt

This page has every measurement of the tracker with its setup. The README has the short table.

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

All these measurements use the rendered mannequin, not a real camera.
