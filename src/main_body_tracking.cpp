// xblick-k4abt: Azure Kinect body tracking from a camera node's streams (body_tracking.hpp), in a process of its
// own: on the camera's host (shared memory) or any other with a GPU (raw frames over the network).
//
//   xblick-k4abt [--base robot/azurekinect] [--model dnn_model_2_0_op11.onnx]
//                    [--processing tensorrt|cuda|cpu] [--gpu-device 0] [--smoothing 0]
//                    [--mask-min-x PX] [--mask-max-x PX] [--mask-max-z MM] [--max-rate HZ] [--tensorrt-cache DIR]
//
// --model defaults to $K4A_BODY_TRACKING_DNN_MODEL_FILEPATH. --tensorrt-cache (TensorRT's engine
// cache; built with XBLICK_K4A_ONNXRUNTIME_DIR only) defaults to $XDG_CACHE_HOME/xblick-k4abt, else
// ~/.cache/xblick-k4abt; an empty value builds the engine at every start (10 s). --processing defaults to tensorrt
// (TensorRT 10 on LD_LIBRARY_PATH). Zenoh: the PC's router (127.0.0.1:7447; the camera on another PC through the
// routers), or --listen/--connect ENDPOINT..., or --zenoh-config FILE. Every option is also a parameter of the node
// <base>/k4abt (xblick/options.hpp): the masks and --max-rate change while it runs, the others are read-only.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>

#include "body_tracking.hpp"
#include "xblick/camera.hpp"
#include "xblick/options.hpp"
#include "xblick/parameters.hpp"

namespace
{
    std::atomic<bool> stop{false};
}  // namespace

int main(int argc, char** argv)
{
    using namespace xblick;
    using namespace xblick::k4a_node;
    std::signal(SIGINT, [](int) { stop = true; });
    std::signal(SIGTERM, [](int) { stop = true; });
    const BodyTrackingOptions defaults;
    const char* env_model = std::getenv("K4A_BODY_TRACKING_DNN_MODEL_FILEPATH");
    Options options("xblick-k4abt", "Azure Kinect body tracking from a camera node's depth and IR streams");
    options.add("base", defaults.base, {.help = "the camera node's base; the parameters' node <base>/k4abt"});
    options.add("model", env_model != nullptr ? env_model : "",
                {.help = "the SDK's ONNX model (default $K4A_BODY_TRACKING_DNN_MODEL_FILEPATH)", .metavar = "FILE"});
    options.add("processing", defaults.processing,
                {.help = "the SDK's processing mode",
                 .parameter = {.choices = {ParameterValue::of("tensorrt"), ParameterValue::of("cuda"),
                                           ParameterValue::of("cpu")}}});
    options.add("gpu-device", std::int64_t{0}, {.help = "the GPU of the tracker"});
    options.add("smoothing", 0.0, {.help = "the SDK's temporal smoothing factor",
                                   .parameter = {.floating_range = FloatingPointRange{0.0, 1.0, 0.0}}});
    // the masks and the rate: BodyTracking::set while it runs (from the next depth + IR pair on)
    options.add("mask-min-x", std::int64_t{-1}, {.help = "depth and IR left of this column masked; 0 or less: off",
                                                 .live = true, .parameter = {.unit = "px"}});
    options.add("mask-max-x", std::int64_t{-1}, {.help = "depth and IR right of this column masked; 0 or less: off",
                                                 .live = true, .parameter = {.unit = "px"}});
    options.add("mask-max-z", std::int64_t{-1}, {.help = "depth beyond this masked; 0 or less: off", .live = true,
                                                 .parameter = {.unit = "mm"}});
    options.add("max-rate", 0.0, {.help = "at most this many pairs a second to the tracker; 0: every pair", .live = true,
                                  .parameter = {.unit = "Hz", .floating_range = FloatingPointRange{0.0, 1000.0, 0.0}}});
    options.add("tensorrt-cache", default_tensorrt_cache(),
                {.help = "TensorRT's engine cache (--processing tensorrt); empty: the engine built at every start",
                 .metavar = "DIR"});
    options.add("status-period", 5.0, {.help = "seconds between the status lines", .parameter = {.unit = "s"}});
    options.add_session_options();
    options.add_parameter_options();
    options.parse(argc, argv);
    try
    {
        zenoh::Session session = open_session(options.session_options());
        const std::string base = check_name(options.text("base"));
        Parameters parameters(session, "k4abt", base);
        parameters.configure(options.parameter_startup());
        // the writers' tracking, once it runs (they run in Zenoh's threads; the guard below clears it under the lock
        // before the tracking goes, also when an exception leaves this scope)
        std::mutex tracker_mutex;
        BodyTracking* tracker = nullptr;
        const auto setting = [&tracker, &tracker_mutex](auto change) {
            return [&tracker, &tracker_mutex, change](const ParameterValue& v) -> std::string {
                std::lock_guard lock(tracker_mutex);
                if (tracker == nullptr)
                {
                    return "the tracking is not running yet";
                }
                BodyTrackingSettings s = tracker->settings();
                change(s, v);
                tracker->set(s);
                return "";
            };
        };
        options.declare(
            parameters,
            {{"mask_min_x", setting([](BodyTrackingSettings& s, const ParameterValue& v) {
                  s.mask_min_x = static_cast<int>(v.integer_value);
              })},
             {"mask_max_x", setting([](BodyTrackingSettings& s, const ParameterValue& v) {
                  s.mask_max_x = static_cast<int>(v.integer_value);
              })},
             {"mask_max_z", setting([](BodyTrackingSettings& s, const ParameterValue& v) {
                  s.mask_max_z_mm = static_cast<int>(v.integer_value);
              })},
             {"max_rate", setting([](BodyTrackingSettings& s, const ParameterValue& v) { s.max_rate = v.double_value; })}},
            {"base"});
        parameters.check_overrides();
        BodyTrackingOptions o;
        o.base = base;
        o.model_path = options.text("model");
        o.processing = options.text("processing");
        o.gpu_device = static_cast<int>(options.integer("gpu-device"));
        o.smoothing = static_cast<float>(options.number("smoothing"));
        o.settings.mask_min_x = static_cast<int>(options.integer("mask-min-x"));
        o.settings.mask_max_x = static_cast<int>(options.integer("mask-max-x"));
        o.settings.mask_max_z_mm = static_cast<int>(options.integer("mask-max-z"));
        o.settings.max_rate = options.number("max-rate");
        if (o.processing == "tensorrt")
        {
            o.tensorrt_cache = options.text("tensorrt-cache");
        }
        else if (options.given("tensorrt-cache"))
        {
            throw std::invalid_argument("--tensorrt-cache is for --processing tensorrt");
        }
        const double status_period = options.number("status-period");

        BodyTracking tracking(session, o);
        struct Unset
        {
            std::mutex& mutex;
            BodyTracking*& tracker;
            ~Unset()
            {
                std::lock_guard lock(mutex);
                tracker = nullptr;
            }
        } unset{tracker_mutex, tracker};  // destroyed before the tracking
        {
            std::lock_guard lock(tracker_mutex);
            tracker = &tracking;
        }
        std::cout << "xblick-k4abt: tracking " << o.base << "/{depth,ir} (" << o.processing << ") -> " << o.base
                  << "/" << kBodies << " (diagnostics: " << o.base << "/diagnostics)" << std::endl;
        std::cout << "ready" << std::endl;

        auto last = std::chrono::steady_clock::now();
        auto last_report = last;
        BodyTrackingStats before = tracking.stats();
        while (not stop)
        {
            tracking.step(std::chrono::milliseconds(200));
            const auto now = std::chrono::steady_clock::now();
            if (now - last_report >= std::chrono::seconds(1))
            {
                tracking.report();  // <base>/diagnostics
                last_report = now;
            }
            const double span = std::chrono::duration<double>(now - last).count();
            if (span >= status_period)
            {
                const BodyTrackingStats s = tracking.stats();
                std::printf("[k4abt] %s\n", describe(s, before, span).c_str());
                std::fflush(stdout);
                before = s;
                last = now;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "xblick-k4abt: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
