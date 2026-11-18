#include "calibration.hpp"

#include <array>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace xblick::k4a_record
{
    namespace
    {
        const std::array<std::pair<k4a_depth_mode_t, const char*>, 6> kDepthModes = {{
            {K4A_DEPTH_MODE_OFF, "OFF"},
            {K4A_DEPTH_MODE_NFOV_2X2BINNED, "NFOV_2X2BINNED"},
            {K4A_DEPTH_MODE_NFOV_UNBINNED, "NFOV_UNBINNED"},
            {K4A_DEPTH_MODE_WFOV_2X2BINNED, "WFOV_2X2BINNED"},
            {K4A_DEPTH_MODE_WFOV_UNBINNED, "WFOV_UNBINNED"},
            {K4A_DEPTH_MODE_PASSIVE_IR, "PASSIVE_IR"},
        }};
        const std::array<std::pair<k4a_color_resolution_t, const char*>, 7> kColorResolutions = {{
            {K4A_COLOR_RESOLUTION_OFF, "OFF"},
            {K4A_COLOR_RESOLUTION_720P, "720P"},
            {K4A_COLOR_RESOLUTION_1080P, "1080P"},
            {K4A_COLOR_RESOLUTION_1440P, "1440P"},
            {K4A_COLOR_RESOLUTION_1536P, "1536P"},
            {K4A_COLOR_RESOLUTION_2160P, "2160P"},
            {K4A_COLOR_RESOLUTION_3072P, "3072P"},
        }};

        template <typename T, std::size_t N>
        std::string name_of(const std::array<std::pair<T, const char*>, N>& table, T value, const char* what)
        {
            for (const auto& [v, name] : table)
            {
                if (v == value)
                {
                    return name;
                }
            }
            throw std::invalid_argument(std::string("unknown ") + what + " " + std::to_string(int(value)));
        }

        template <typename T, std::size_t N>
        T value_of(const std::array<std::pair<T, const char*>, N>& table, const std::string& name, const char* what)
        {
            std::string names;
            for (const auto& [v, n] : table)
            {
                if (name == n)
                {
                    return v;
                }
                names += std::string(names.empty() ? "" : ", ") + n;
            }
            throw std::invalid_argument(std::string(what) + " '" + name + "': one of " + names);
        }
    }  // namespace

    std::string depth_mode_name(k4a_depth_mode_t mode) { return name_of(kDepthModes, mode, "depth mode"); }
    k4a_depth_mode_t depth_mode_from_name(const std::string& name)
    {
        return value_of(kDepthModes, name, "depth mode");
    }
    std::string color_resolution_name(k4a_color_resolution_t r)
    {
        return name_of(kColorResolutions, r, "colour resolution");
    }
    k4a_color_resolution_t color_resolution_from_name(const std::string& name)
    {
        return value_of(kColorResolutions, name, "colour resolution");
    }

    namespace
    {
        nlohmann::json file_json(const std::optional<xblick::CameraInfo>& file)
        {
            if (not file)
            {
                return nullptr;
            }
            return {{"width", file->width}, {"height", file->height}, {"distortion_model", file->distortion_model},
                    {"d", file->d},         {"k", file->k},           {"r", file->r},
                    {"p", file->p}};
        }

        std::optional<xblick::CameraInfo> file_from_json(const nlohmann::json& j)
        {
            if (j.is_null())
            {
                return std::nullopt;
            }
            xblick::CameraInfo info;
            info.width = j.at("width").get<std::uint32_t>();
            info.height = j.at("height").get<std::uint32_t>();
            info.distortion_model = j.at("distortion_model").get<std::string>();
            info.d = j.at("d").get<std::vector<double>>();
            info.k = j.at("k").get<std::array<double, 9>>();
            info.r = j.at("r").get<std::array<double, 9>>();
            info.p = j.at("p").get<std::array<double, 12>>();
            return info;
        }

        // K and D of a calibration file into the SDK's camera (its Brown-Conrady model is rational_polynomial;
        // plumb_bob is the same with k4 = k5 = k6 = 0)
        void use_file(k4a_calibration_camera_t& camera, const xblick::CameraInfo& file, const std::string& which)
        {
            const std::string what = which + " calibration file: ";
            if (file.width != std::uint32_t(camera.resolution_width) or
                file.height != std::uint32_t(camera.resolution_height))
            {
                throw std::invalid_argument(what + std::to_string(file.width) + " x " + std::to_string(file.height) +
                                            ", the camera's mode gives " + std::to_string(camera.resolution_width) +
                                            " x " + std::to_string(camera.resolution_height));
            }
            const bool rational = file.distortion_model == "rational_polynomial" and file.d.size() == 8;
            const bool plumb_bob = file.distortion_model == "plumb_bob" and file.d.size() == 5;
            if (not rational and not plumb_bob)
            {
                throw std::invalid_argument(what + "distortion model " + file.distortion_model + " with " +
                                            std::to_string(file.d.size()) +
                                            " coefficients; rational_polynomial (8) or plumb_bob (5)");
            }
            const auto& k = file.k;
            if (k[1] != 0.0 or k[3] != 0.0 or k[6] != 0.0 or k[7] != 0.0 or k[8] != 1.0 or not(k[0] > 0.0) or
                not(k[4] > 0.0))
            {
                throw std::invalid_argument(what + "K must be [fx 0 cx; 0 fy cy; 0 0 1] (no skew)");
            }
            if (file.r != std::array<double, 9>{1, 0, 0, 0, 1, 0, 0, 0, 1})
            {
                throw std::invalid_argument(what +
                                            "R must be the identity (a camera of its own, not of a stereo pair)");
            }
            auto& p = camera.intrinsics.parameters.param;
            p = {};
            p.fx = float(k[0]);
            p.fy = float(k[4]);
            p.cx = float(k[2]);
            p.cy = float(k[5]);
            p.k1 = float(file.d[0]);
            p.k2 = float(file.d[1]);
            p.p1 = float(file.d[2]);
            p.p2 = float(file.d[3]);
            p.k3 = float(file.d[4]);
            if (rational)
            {
                p.k4 = float(file.d[5]);
                p.k5 = float(file.d[6]);
                p.k6 = float(file.d[7]);
            }
            camera.intrinsics.type = K4A_CALIBRATION_LENS_DISTORTION_MODEL_BROWN_CONRADY;
            camera.intrinsics.parameter_count = 14;
        }
    }  // namespace

    std::string CalibrationRecord::to_json() const
    {
        return nlohmann::json{{"v", kCalibrationRecordVersion},
                              {"sensor", "azure_kinect"},
                              {"raw", raw},
                              {"depth_mode", depth_mode_name(depth_mode)},
                              {"color_resolution", color_resolution_name(color_resolution)},
                              {"fps", fps},
                              {"serial", serial},
                              {"depth_frame_id", depth_frame_id},
                              {"color_frame_id", color_frame_id},
                              {"color_file", file_json(color_file)},
                              {"depth_file", file_json(depth_file)}}
            .dump();
    }

    CalibrationRecord CalibrationRecord::from_json(const std::string& text)
    {
        const nlohmann::json j = nlohmann::json::parse(text);
        if (j.value("v", 0) != kCalibrationRecordVersion)
        {
            throw std::runtime_error("calibration record of version " + j.value("v", nlohmann::json(nullptr)).dump() +
                                     ", this program reads " + std::to_string(kCalibrationRecordVersion));
        }
        CalibrationRecord r;
        r.raw = j.at("raw").get<std::string>();
        r.depth_mode = depth_mode_from_name(j.at("depth_mode").get<std::string>());
        r.color_resolution = color_resolution_from_name(j.at("color_resolution").get<std::string>());
        r.fps = j.at("fps").get<int>();
        r.serial = j.at("serial").get<std::string>();
        r.depth_frame_id = j.at("depth_frame_id").get<std::string>();
        r.color_frame_id = j.at("color_frame_id").get<std::string>();
        r.color_file = file_from_json(j.at("color_file"));
        r.depth_file = file_from_json(j.at("depth_file"));
        return r;
    }

    k4a_calibration_t CalibrationRecord::calibration() const
    {
        k4a_calibration_t calibration;
        // the SDK reads the terminating zero as part of the buffer (k4a_device_get_raw_calibration includes it)
        if (k4a_calibration_get_from_raw(const_cast<char*>(raw.c_str()), raw.size() + 1, depth_mode,
                                         color_resolution, &calibration) != K4A_RESULT_SUCCEEDED)
        {
            throw std::runtime_error("k4a_calibration_get_from_raw failed for depth mode " +
                                     depth_mode_name(depth_mode) + ", colour " +
                                     color_resolution_name(color_resolution));
        }
        if (color_file)
        {
            use_file(calibration.color_camera_calibration, *color_file, "colour");
        }
        if (depth_file)
        {
            use_file(calibration.depth_camera_calibration, *depth_file, "depth");
        }
        return calibration;
    }
}  // namespace xblick::k4a_record
