// An Azure Kinect's calibration for Durchblick: the ROS CameraInfo of its depth (and IR) camera and of its colour
// camera, and the calibration record that lets another process rebuild the SDK's k4a_calibration_t exactly (the body
// tracker, a point cloud): the device's raw calibration (JSON text, k4a_device_get_raw_calibration) with the depth
// mode and colour resolution, which is what k4a_calibration_get_from_raw() takes.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <k4a/k4a.h>

#include "durchblick/msg.hpp"

namespace durchblick::k4a_node
{
    // Names of the SDK's modes, as the options and the calibration record spell them.
    std::string depth_mode_name(k4a_depth_mode_t mode);
    k4a_depth_mode_t depth_mode_from_name(const std::string& name);
    std::string color_resolution_name(k4a_color_resolution_t resolution);
    k4a_color_resolution_t color_resolution_from_name(const std::string& name);
    k4a_fps_t fps_from_value(int fps);
    int fps_value(k4a_fps_t fps);

    // ROS CameraInfo of one camera of the calibration (k4a_calibration_t::depth_camera_calibration or
    // ::color_camera_calibration), at the resolution of the mode it was made for. The SDK's Brown-Conrady model is
    // OpenCV's rational model: distortion_model "rational_polynomial", D = [k1, k2, p1, p2, k3, k4, k5, k6]. The
    // deprecated Rational 6KT model (early internal devices) has other tangential terms and is refused.
    durchblick::CameraInfo camera_info(const k4a_calibration_camera_t& camera, const std::string& frame_id);

    struct CalibrationRecord
    {
        std::string raw;  // the device's raw calibration, JSON text
        k4a_depth_mode_t depth_mode = K4A_DEPTH_MODE_OFF;
        k4a_color_resolution_t color_resolution = K4A_COLOR_RESOLUTION_OFF;
        int fps = 0;
        std::string serial;
        std::string depth_frame_id;
        std::string color_frame_id;
        // ROS calibration files (CameraInfo::read_file) that replace the device's intrinsics of the colour and of the
        // depth camera (the IR image is the depth camera's), as a ROS driver's camera_info_url: calibration() puts
        // their K and D into the SDK's calibration, so depth_to_color, the body tracker and the point clouds use them;
        // color_info() and depth_info() are the files' CameraInfo themselves (their R and P too). The extrinsics
        // (tf_static) stay the device's.
        std::optional<durchblick::CameraInfo> color_file, depth_file;

        // JSON for <base>/k4a_calibration: {"v": 1, "raw": ..., "depth_mode": "NFOV_UNBINNED", ...}
        std::string to_json() const;
        static CalibrationRecord from_json(const std::string& text);
        // The SDK's calibration, as the device would give it for these modes, with the files' intrinsics. Throws for
        // a file that does not fit its camera: another size than the mode's, a model other than rational_polynomial
        // (the SDK's) or plumb_bob, skew, or R not the identity.
        k4a_calibration_t calibration() const;
        // The CameraInfo of the colour and the depth camera's streams: the file's, else camera_info() of the device's
        durchblick::CameraInfo color_info() const;
        durchblick::CameraInfo depth_info() const;
    };

    // The record with the calibration files of the colour and the depth camera read (an empty path: the device's);
    // throws for a file that cannot be read or does not fit (calibration()).
    CalibrationRecord with_calibration_files(CalibrationRecord record, const std::string& color_path,
                                             const std::string& depth_path);

    // 2: the calibration files (color_file, depth_file)
    inline constexpr int kCalibrationRecordVersion = 2;
}  // namespace durchblick::k4a_node
