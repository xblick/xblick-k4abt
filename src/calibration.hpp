// The reading side of an Azure Kinect node's calibration record: what <base>/k4a_calibration of xblick-azure-kinect
// answers, and from which another process rebuilds the SDK's k4a_calibration_t exactly. The record holds the device's
// raw calibration (JSON text, k4a_device_get_raw_calibration) with the depth mode and colour resolution, which is what
// k4a_calibration_get_from_raw() takes, and the intrinsics of the node's calibration files.
//
// A copy of xblick-azure-kinect's src/calibration.* (the camera defines the record), reduced to what the tracker and
// its test need, in the namespace xblick::k4a_record so that a program can link the camera's library and this one.
// Keep the record's format and kCalibrationRecordVersion in step with the camera's.
#pragma once

#include <optional>
#include <string>

#include <k4a/k4a.h>

#include "xblick/msg.hpp"

namespace xblick::k4a_record
{
    // Names of the SDK's modes, as the calibration record spells them.
    std::string depth_mode_name(k4a_depth_mode_t mode);
    k4a_depth_mode_t depth_mode_from_name(const std::string& name);
    std::string color_resolution_name(k4a_color_resolution_t resolution);
    k4a_color_resolution_t color_resolution_from_name(const std::string& name);

    struct CalibrationRecord
    {
        std::string raw;  // the device's raw calibration, JSON text
        k4a_depth_mode_t depth_mode = K4A_DEPTH_MODE_OFF;
        k4a_color_resolution_t color_resolution = K4A_COLOR_RESOLUTION_OFF;
        int fps = 0;
        std::string serial;
        std::string depth_frame_id;
        std::string color_frame_id;
        // the ROS calibration files of the camera node (its --color-calibration, --depth-calibration): calibration()
        // puts their K and D into the SDK's calibration, so that the tracker uses the intrinsics the node uses
        std::optional<xblick::CameraInfo> color_file, depth_file;

        // JSON for <base>/k4a_calibration: {"v": 2, "raw": ..., "depth_mode": "NFOV_UNBINNED", ...}
        std::string to_json() const;
        static CalibrationRecord from_json(const std::string& text);
        // The SDK's calibration, as the device would give it for these modes, with the files' intrinsics. Throws for
        // a file that does not fit its camera: another size than the mode's, a model other than rational_polynomial
        // (the SDK's) or plumb_bob, skew, or R not the identity.
        k4a_calibration_t calibration() const;
    };

    // 2: the calibration files (color_file, depth_file)
    inline constexpr int kCalibrationRecordVersion = 2;
}  // namespace xblick::k4a_record
