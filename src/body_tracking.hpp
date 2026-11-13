// Body tracking decoupled from the camera: the Azure Kinect Body Tracking SDK fed from the camera node's streams
// instead of the device. The tracker takes a k4a capture with the depth and IR image of one exposure and the
// calibration; both cross the process boundary without loss: depth and IR as 16-bit frames over Durchblick (shared
// memory on the camera's host), paired by their common stamp, and the calibration as the node's calibration record
// (k4a_calibration_get_from_raw rebuilds exactly the device's calibration).
//
// Results go out as JSON on <base>/bodies (model K4ABT_BODY_32, the SDK's 32 joints, positions in mm and orientations
// in the depth camera's frame):
//
//   {"v": 1, "model": "K4ABT_BODY_32", "stamp_ns": ..., "frame_id": "AzureKinectDepthCamera", "latency_ms": ...,
//    "bodies": [{"id": 1, "joints": {"Pelvis": {"position_mm": [x, y, z], "orientation_wxyz": [w, x, y, z],
//                                               "confidence": 2}, ...}}]}
//
// confidence is the SDK's level: 0 none (out of view), 1 low (occluded, predicted), 2 medium, 3 high (not yet
// produced by SDK 1.1). The same results go to BodyTrackingOptions::on_result (for a program that embeds the
// tracker).
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <zenoh.hxx>

#include "durchblick/msg.hpp"

namespace durchblick::k4a_node
{
    inline constexpr std::size_t kJointCount = 32;  // K4ABT_JOINT_COUNT

    // the joints of the pose model K4ABT_BODY_32, in the SDK's joint order (k4abt_joint_id_t)
    inline constexpr std::array<const char*, kJointCount> kJointNames = {
        "Pelvis",     "SpineNaval",  "SpineChest", "Neck",         "ClavicleLeft",  "ShoulderLeft", "ElbowLeft",
        "WristLeft",  "HandLeft",    "HandtipLeft", "ThumbLeft",   "ClavicleRight", "ShoulderRight", "ElbowRight",
        "WristRight", "HandRight",   "HandtipRight", "ThumbRight", "HipLeft",       "KneeLeft",     "AnkleLeft",
        "FootLeft",   "HipRight",    "KneeRight",  "AnkleRight",   "FootRight",     "Head",         "Nose",
        "EyeLeft",    "EarLeft",     "EyeRight",   "EarRight"};

    struct BodyJoint
    {
        std::array<float, 3> position_mm;
        std::array<float, 4> orientation_wxyz;
        int confidence;  // the SDK's level (above)
    };

    struct Body
    {
        std::uint32_t id;
        std::array<BodyJoint, kJointCount> joints;  // in kJointNames' order
    };

    // one result of the tracker: the bodies of one capture, in the depth camera's frame
    struct BodyResult
    {
        std::int64_t stamp_ns;  // the capture's stamp (the depth frame's)
        std::string frame_id;   // the depth camera's frame (the node's calibration record)
        double latency_ms;      // capture stamp to result
        std::vector<Body> bodies;
    };

    // What can change while the tracking runs (BodyTracking::set): it takes effect at the next depth + IR pair.
    struct BodyTrackingSettings
    {
        // off: the tracker drops its depth and IR subscriptions (the camera node stops producing them for it), keeps
        // its model and GPU memory, and step() only waits; on again: it subscribes again
        bool enabled = true;
        // masks: depth and IR outside are set to 65535 before tracking, so that bodies there are not tracked; 0 or
        // negative: off
        int mask_min_x = -1, mask_max_x = -1, mask_max_z_mm = -1;
        // at most this many pairs per second go to the tracker (the others are skipped); 0: every pair
        double max_rate = 0.0;
        bool operator==(const BodyTrackingSettings&) const = default;
    };

    struct BodyTrackingOptions
    {
        std::string base = "robot/azurekinect";
        std::string model_path;          // the SDK's ONNX model (dnn_model_2_0_op11.onnx or the lite one)
        std::string processing = "tensorrt";  // cpu, cuda, tensorrt (the SDK's processing modes on Linux)
        int gpu_device = 0;
        float smoothing = 0.0f;  // the SDK's temporal smoothing factor
        // TensorRT's engine cache (created if missing; only with DURCHBLICK_K4A_ONNXRUNTIME_DIR); empty: the engine is
        // built at every start. Used with processing "tensorrt" only. The engines go into a subdirectory named after
        // the ONNX Runtime and TensorRT versions; subdirectories of other versions are deleted.
        std::string tensorrt_cache;
        BodyTrackingSettings settings;  // at the start
        std::chrono::milliseconds calibration_timeout{10000};
        // called with every result, in the results thread, after it went out on <base>/bodies; what it throws stops
        // the tracking (the next step() throws it)
        std::function<void(const BodyResult&)> on_result;
    };

    // $XDG_CACHE_HOME/durchblick-k4abt, else ~/.cache/durchblick-k4abt (throws if neither is set); "" in builds
    // without DURCHBLICK_K4A_ONNXRUNTIME_DIR, which have no engine cache
    std::string default_tensorrt_cache();

    inline constexpr const char* kBodies = "bodies";
    inline constexpr int kBodiesVersion = 1;

    struct BodyTrackingStats
    {
        std::uint64_t pairs = 0;        // depth + IR pairs received
        std::uint64_t unpaired = 0;     // depth frames whose IR did not come
        std::uint64_t enqueued = 0;     // pairs given to the tracker
        std::uint64_t queue_full = 0;   // pairs dropped because the tracker was busy (only the newest counts)
        std::uint64_t rate_limited = 0; // depth frames skipped for BodyTrackingSettings::max_rate
        std::uint64_t results = 0;      // results published
        std::uint64_t bodies = 0;       // bodies in them
        double latency_ms = 0;          // capture stamp to result, summed
    };

    // "12.0 pairs/s, 12.0 results/s, ...": the rates between two stats() taken `seconds` apart
    std::string describe(const BodyTrackingStats& now, const BodyTrackingStats& before, double seconds);

    class BodyTracking
    {
    public:
        BodyTracking(zenoh::Session& session, const BodyTrackingOptions& options);
        ~BodyTracking();
        BodyTracking(const BodyTracking&) = delete;
        BodyTracking& operator=(const BodyTracking&) = delete;

        // Takes the next depth + IR pair (waiting up to `timeout`) and gives it to the tracker. Results are published
        // from a thread of their own as soon as the tracker has them; that thread's failure is thrown here. Returns
        // false if no pair came.
        bool step(std::chrono::milliseconds timeout);
        BodyTrackingStats stats() const;
        // Thread-safe; takes effect at the next step().
        void set(const BodyTrackingSettings& settings);
        BodyTrackingSettings settings() const;
        // How the tracking did since the last report, as the status "body_tracking" on <base>/diagnostics (beside the
        // camera's), with `extra` values appended; returned for a log or another monitor. About once a second.
        DiagnosticStatus report(const std::vector<KeyValue>& extra = {});

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace durchblick::k4a_node
