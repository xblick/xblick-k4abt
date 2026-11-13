#include "body_tracking.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include <dlfcn.h>

#include <k4abt.hpp>
#include <nlohmann/json.hpp>

#include "calibration.hpp"
#include "xblick/camera.hpp"
#include "xblick/protocol.hpp"
#include "node.hpp"

// ONNX Runtime's entry point (onnxruntime_c_api.h), from the library libk4abt runs on: the SDK's own 1.10 or a
// current one behind src/onnxruntime_compat.cpp. OrtApiBase is {GetApi, GetVersionString} in every version.
extern "C"
{
    struct OrtApiBase
    {
        const void* (*GetApi)(std::uint32_t version);
        const char* (*GetVersionString)();
    };
    const OrtApiBase* OrtGetApiBase();
#ifdef XBLICK_K4A_ONNXRUNTIME_COMPAT
    void xblick_onnxruntime_tensorrt_cache(const char* dir) noexcept;  // src/onnxruntime_compat.cpp
#endif
}

namespace xblick::k4a_node
{
    namespace
    {
        static_assert(kJointCount == K4ABT_JOINT_COUNT);

        k4abt_tracker_processing_mode_t processing_mode(const std::string& name)
        {
            if (name == "cpu") return K4ABT_TRACKER_PROCESSING_MODE_CPU;
            if (name == "cuda") return K4ABT_TRACKER_PROCESSING_MODE_GPU_CUDA;
            if (name == "tensorrt") return K4ABT_TRACKER_PROCESSING_MODE_GPU_TENSORRT;
            throw std::invalid_argument("processing mode '" + name + "': cpu, cuda or tensorrt");
        }

        // The GPU's compute capability through the driver (libcuda.so.1, present wherever an NVIDIA driver is).
        std::pair<int, int> compute_capability(int device)
        {
            void* cuda = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
            if (cuda == nullptr)
            {
                throw std::runtime_error("GPU processing: no NVIDIA driver (libcuda.so.1 not found)");
            }
            using Init = int (*)(unsigned);
            using DeviceGet = int (*)(int*, int);
            using Attribute = int (*)(int*, int, int);
            const auto init = reinterpret_cast<Init>(dlsym(cuda, "cuInit"));
            const auto get = reinterpret_cast<DeviceGet>(dlsym(cuda, "cuDeviceGet"));
            const auto attribute = reinterpret_cast<Attribute>(dlsym(cuda, "cuDeviceGetAttribute"));
            int handle = 0, major = 0, minor = 0;
            constexpr int kMajor = 75, kMinor = 76;  // CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR / _MINOR
            if (init == nullptr or get == nullptr or attribute == nullptr or init(0) != 0 or
                get(&handle, device) != 0 or attribute(&major, kMajor, handle) != 0 or
                attribute(&minor, kMinor, handle) != 0)
            {
                throw std::runtime_error("GPU processing: cannot query CUDA device " + std::to_string(device));
            }
            return {major, minor};
        }

        // The Body Tracking SDK 1.1 runs its network with ONNX Runtime; the one it brings (1.10) has CUDA 11 and no
        // kernels for GPUs of compute capability 10 and newer. There every inference fails, and the SDK still
        // delivers results: frames without bodies, as if nobody was in view (measured 2026-10-27 on an RTX PRO 6000,
        // compute capability 12.0: cudaErrorNoKernelImageForDevice). Refuse that combination instead of tracking
        // nothing; a current ONNX Runtime (XBLICK_K4A_ONNXRUNTIME_DIR) has kernels for these GPUs.
        void check_gpu(const std::string& processing, int device)
        {
            const std::string onnxruntime = OrtGetApiBase()->GetVersionString();
            std::cerr << "xblick-k4abt: ONNX Runtime " << onnxruntime << std::endl;
            if (processing == "cpu")
            {
                return;
            }
            const auto [major, minor] = compute_capability(device);
            std::cerr << "xblick-k4abt: GPU " << device << " has compute capability " << major << "." << minor
                      << std::endl;
            if (major >= 10 and onnxruntime.starts_with("1.10."))
            {
                throw std::runtime_error(
                    "GPU " + std::to_string(device) + " has compute capability " + std::to_string(major) + "." +
                    std::to_string(minor) + ": the ONNX Runtime 1.10 (CUDA 11) of the Body Tracking SDK 1.1 has no "
                    "kernels for it (every result would come without bodies). Build with "
                    "XBLICK_K4A_ONNXRUNTIME_DIR (a current ONNX Runtime for CUDA 12) or use --processing cpu");
            }
        }

#ifdef XBLICK_K4A_ONNXRUNTIME_COMPAT
        // "onnxruntime-1.31.1_tensorrt-10.14.1.48": the versions an engine was built with. TensorRT's version through
        // libnvinfer.so.10, the TensorRT the execution provider of ONNX Runtime's CUDA 12 packages loads.
        std::string engine_versions()
        {
            void* nvinfer = dlopen("libnvinfer.so.10", RTLD_NOW | RTLD_LOCAL);
            if (nvinfer == nullptr)
            {
                throw std::runtime_error(std::string("processing tensorrt needs TensorRT 10 on "
                                                     "LD_LIBRARY_PATH: ") + dlerror());
            }
            using Version = std::int32_t (*)();
            const auto version = reinterpret_cast<Version>(dlsym(nvinfer, "getInferLibVersion"));
            const auto build = reinterpret_cast<Version>(dlsym(nvinfer, "getInferLibBuildVersion"));
            if (version == nullptr or build == nullptr)
            {
                throw std::runtime_error("libnvinfer.so.10 has no getInferLibVersion/getInferLibBuildVersion");
            }
            const std::int32_t v = version();  // (major * 100 + minor) * 100 + patch
            return "onnxruntime-" + std::string(OrtGetApiBase()->GetVersionString()) + "_tensorrt-" +
                   std::to_string(v / 10000) + "." + std::to_string(v / 100 % 100) + "." + std::to_string(v % 100) +
                   "." + std::to_string(build());
        }

        // The engine cache of these versions under `base`, created; the ones of other versions under it are deleted
        // (engines of another TensorRT do not load). Only subdirectories named like engine_versions() are touched.
        std::filesystem::path engine_cache(const std::filesystem::path& base)
        {
            const std::string current = engine_versions();
            std::filesystem::create_directories(base);
            for (const auto& entry : std::filesystem::directory_iterator(base))
            {
                const std::string name = entry.path().filename().string();
                if (entry.is_directory() and name != current and name.starts_with("onnxruntime-") and
                    name.find("_tensorrt-") != std::string::npos)
                {
                    std::filesystem::remove_all(entry.path());
                    std::cerr << "xblick-k4abt: deleted the TensorRT engine cache of " << name << std::endl;
                }
            }
            const std::filesystem::path dir = base / current;
            std::filesystem::create_directories(dir);
            return dir;
        }
#endif

        // a 16-bit frame of the stream as an SDK image of its own (the frame's rows may be padded)
        k4a::image to_k4a(const Frame& frame, k4a_image_format_t format)
        {
            const int w = int(frame.width), h = int(frame.height);
            k4a::image image = k4a::image::create(format, w, h, w * 2);
            for (int y = 0; y < h; ++y)
            {
                std::memcpy(image.get_buffer() + std::size_t(y) * w * 2, frame.data() + std::size_t(y) * frame.step,
                            std::size_t(w) * 2);
            }
            return image;
        }
    }  // namespace

    struct BodyTracking::Impl
    {
        BodyTrackingOptions o;
        CalibrationRecord record;
        k4a_calibration_t calibration{};
        k4abt::tracker tracker;
        zenoh::Session* session = nullptr;
        std::string base;
        std::unique_ptr<ImageSubscriber> depth, ir;  // null while the tracking is off
        BodyTrackingSettings settings;  // guarded by mutex: set() writes it from other threads
        std::chrono::steady_clock::time_point last_enqueued{};
        std::optional<zenoh::Publisher> bodies;
        std::int64_t bodies_seq = 0;  // the attachment's seq of <base>/bodies (protocol.hpp)
        std::unique_ptr<DiagnosticsPublisher> diagnostics;
        std::chrono::steady_clock::time_point reported;
        BodyTrackingStats reported_stats;
        std::optional<Frame> early_ir;  // an IR frame newer than the depth frame in hand: it waits for its depth
        int width = 0, height = 0;
        // Results are taken in a thread of their own and published as soon as the tracker has them; taken between
        // two pairs, each waited for the next frame (measured 2026-10-27: 36 ms from capture to result where the SDK
        // needs 7 ms with TensorRT, 16 ms with CUDA).
        std::thread results;
        std::atomic<bool> stopping{false};
        std::mutex mutex;  // guards stamps, stats and error, shared by step() and the results thread
        std::deque<std::pair<std::uint64_t, std::int64_t>> stamps;  // device µs given to the tracker -> stamp ns
        BodyTrackingStats stats;
        std::exception_ptr error;  // the results thread's failure, thrown by the next step()

        void take_results()
        {
            try
            {
                while (true)
                {
                    k4abt::frame result;
                    bool popped = false;
                    try
                    {
                        popped = tracker.pop_result(&result, std::chrono::milliseconds(100));
                    }
                    catch (const k4a::error&)
                    {
                        if (stopping)
                        {
                            return;  // shut down and the queue empty
                        }
                        throw;
                    }
                    if (popped)
                    {
                        publish(result);
                    }
                    else if (stopping)
                    {
                        return;
                    }
                }
            }
            catch (...)
            {
                std::lock_guard lock(mutex);
                error = std::current_exception();
            }
        }

        void check(const Frame& frame, const char* encoding) const
        {
            if (frame.encoding != encoding or int(frame.width) != width or int(frame.height) != height)
            {
                throw std::runtime_error(std::string("expected ") + encoding + " " + std::to_string(width) + "x" +
                                         std::to_string(height) + " (the calibration's depth mode), got " +
                                         frame.encoding + " " + std::to_string(frame.width) + "x" +
                                         std::to_string(frame.height) + " on " + frame.key);
            }
        }

        // the IR frame of this depth frame's exposure, waiting up to 100 ms for it
        std::optional<Frame> matching_ir(std::int64_t stamp)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            while (true)
            {
                if (early_ir)
                {
                    if (early_ir->stamp_ns() == stamp)
                    {
                        return std::exchange(early_ir, std::nullopt);
                    }
                    if (early_ir->stamp_ns() > stamp)
                    {
                        return std::nullopt;  // this depth frame's IR is gone (overwritten): skip it
                    }
                    early_ir.reset();
                }
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                if (left.count() <= 0)
                {
                    return std::nullopt;
                }
                early_ir = ir->latest(true, left);
            }
        }

        // Auto: shared memory on the camera's host (never its CUDA ring: the Body Tracking SDK takes host images, and
        // local Auto, the default since Durchblick 0.3.38, would take a ring on the same GPU), raw frames from another
        // host
        void subscribe()
        {
            const SubscriberOptions raw{.transport = Transport::Auto, .local = Transport::Shm, .remote = Transport::Raw};
            depth = std::make_unique<ImageSubscriber>(*session, base + "/" + kDepth, raw);
            ir = std::make_unique<ImageSubscriber>(*session, base + "/" + kIr, raw);
        }

        void unsubscribe()
        {
            depth.reset();
            ir.reset();
            early_ir.reset();
        }

        void mask(k4a::image& depth_image, k4a::image& ir_image, const BodyTrackingSettings& s) const
        {
            if (s.mask_min_x <= 0 and s.mask_max_x <= 0 and s.mask_max_z_mm <= 0)
            {
                return;
            }
            auto* d = reinterpret_cast<std::uint16_t*>(depth_image.get_buffer());
            auto* r = reinterpret_cast<std::uint16_t*>(ir_image.get_buffer());
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const std::size_t i = std::size_t(y) * width + x;
                    if ((s.mask_min_x > 0 and x < s.mask_min_x) or (s.mask_max_x > 0 and x > s.mask_max_x) or
                        (s.mask_max_z_mm > 0 and d[i] > s.mask_max_z_mm))
                    {
                        d[i] = 0xFFFF;
                        r[i] = 0xFFFF;
                    }
                }
            }
        }

        void publish(const k4abt::frame& frame)
        {
            // the attachment's publish_called_ns: the tracker's result is here, before it is turned into JSON
            const std::int64_t called_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count();
            const auto device = static_cast<std::uint64_t>(frame.get_device_timestamp().count());
            std::int64_t stamp = static_cast<std::int64_t>(device) * 1000;
            {
                std::lock_guard lock(mutex);
                for (const auto& [us, ns] : stamps)
                {
                    if (us == device)
                    {
                        stamp = ns;  // the frames' stamp to the nanosecond
                    }
                }
            }
            const double latency =
                double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count() -
                       stamp) /
                1e6;
            BodyResult result{
                .stamp_ns = stamp, .frame_id = record.depth_frame_id, .latency_ms = latency, .bodies = {}};
            const std::uint32_t n = frame.get_num_bodies();
            for (std::uint32_t b = 0; b < n; ++b)
            {
                const k4abt_body_t body = frame.get_body(b);
                Body& out = result.bodies.emplace_back(Body{.id = body.id, .joints = {}});
                for (std::size_t j = 0; j < kJointCount; ++j)
                {
                    const k4abt_joint_t& joint = body.skeleton.joints[j];
                    out.joints[j] = {.position_mm = {joint.position.v[0], joint.position.v[1], joint.position.v[2]},
                                     .orientation_wxyz = {joint.orientation.wxyz.w, joint.orientation.wxyz.x,
                                                          joint.orientation.wxyz.y, joint.orientation.wxyz.z},
                                     .confidence = int(joint.confidence_level)};
                }
            }
            // with the protocol's attachment, as every Durchblick sample: its publish_ns against the result's stamp is
            // how long the capture took to come out of this node
            zenoh::Publisher::PutOptions options;
            options.attachment = zenoh::Bytes(nlohmann::json{{"v", xblick::protocol::kAttachmentVersion},
                                                             {"seq", bodies_seq++},
                                                             {"publish_called_ns", called_ns},
                                                             {"publish_ns", std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                                                std::chrono::system_clock::now().time_since_epoch())
                                                                                .count()},
                                                             {"source", ""}}
                                                  .dump());
            bodies->put(zenoh::Bytes(to_json(result).dump()), std::move(options));
            if (o.on_result)
            {
                o.on_result(result);
            }
            std::lock_guard lock(mutex);
            ++stats.results;
            stats.bodies += n;
            stats.latency_ms += latency;
        }

        static nlohmann::json to_json(const BodyResult& result)
        {
            nlohmann::json bodies_json = nlohmann::json::array();
            for (const Body& body : result.bodies)
            {
                nlohmann::json joints = nlohmann::json::object();
                for (std::size_t j = 0; j < kJointCount; ++j)
                {
                    const BodyJoint& joint = body.joints[j];
                    joints[kJointNames[j]] = {{"position_mm", joint.position_mm},
                                              {"orientation_wxyz", joint.orientation_wxyz},
                                              {"confidence", joint.confidence}};
                }
                bodies_json.push_back({{"id", body.id}, {"joints", std::move(joints)}});
            }
            return {{"v", kBodiesVersion},         {"model", "K4ABT_BODY_32"},
                    {"stamp_ns", result.stamp_ns}, {"frame_id", result.frame_id},
                    {"latency_ms", result.latency_ms}, {"bodies", std::move(bodies_json)}};
        }
    };

    BodyTracking::BodyTracking(zenoh::Session& session, const BodyTrackingOptions& options)
        : impl_(std::make_unique<Impl>())
    {
        Impl& m = *impl_;
        m.o = options;
        if (options.model_path.empty())
        {
            throw std::invalid_argument("no body tracking model (--model: the SDK's dnn_model_2_0_op11.onnx)");
        }
        m.record = query_calibration(session, options.base, options.calibration_timeout);
        m.calibration = m.record.calibration();
        m.width = m.calibration.depth_camera_calibration.resolution_width;
        m.height = m.calibration.depth_camera_calibration.resolution_height;
        check_gpu(options.processing, options.gpu_device);
        if (options.processing == "tensorrt" and not options.tensorrt_cache.empty())
        {
#ifdef XBLICK_K4A_ONNXRUNTIME_COMPAT
            const std::filesystem::path dir = engine_cache(options.tensorrt_cache);
            xblick_onnxruntime_tensorrt_cache(dir.c_str());
            std::cerr << "xblick-k4abt: TensorRT engine cache " << dir.string() << std::endl;
#else
            throw std::invalid_argument("a TensorRT engine cache needs XBLICK_K4A_ONNXRUNTIME_DIR (this build "
                                        "runs on the Body Tracking SDK's own ONNX Runtime)");
#endif
        }
        k4abt_tracker_configuration_t config = K4ABT_TRACKER_CONFIG_DEFAULT;
        config.processing_mode = processing_mode(options.processing);
        config.gpu_device_id = options.gpu_device;
        config.model_path = options.model_path.c_str();
        m.tracker = k4abt::tracker::create(m.calibration, config);
        m.tracker.set_temporal_smoothing(options.smoothing);
        if (options.settings.max_rate < 0.0)
        {
            throw std::invalid_argument("max_rate " + std::to_string(options.settings.max_rate) +
                                        ": 0 (every pair) or more");
        }
        m.session = &session;
        m.base = check_name(options.base);
        m.settings = options.settings;
        if (m.settings.enabled)
        {
            m.subscribe();
        }
        m.bodies = session.declare_publisher(zenoh::KeyExpr(m.base + "/" + kBodies));
        m.diagnostics = std::make_unique<DiagnosticsPublisher>(session, m.base);
        m.reported = std::chrono::steady_clock::now();
        m.results = std::thread([&m] { m.take_results(); });
    }

    BodyTracking::~BodyTracking()
    {
        if (impl_ and impl_->tracker)
        {
            impl_->stopping = true;
            impl_->tracker.shutdown();
        }
        if (impl_ and impl_->results.joinable())
        {
            impl_->results.join();
        }
    }

    bool BodyTracking::step(std::chrono::milliseconds timeout)
    {
        Impl& m = *impl_;
        BodyTrackingSettings s;
        {
            std::lock_guard lock(m.mutex);
            if (m.error)
            {
                std::rethrow_exception(m.error);
            }
            s = m.settings;
        }
        if (s.enabled != bool(m.depth))
        {
            s.enabled ? m.subscribe() : m.unsubscribe();
        }
        if (not s.enabled)
        {
            std::this_thread::sleep_for(timeout);
            return false;
        }
        std::optional<Frame> depth = m.depth->latest(true, timeout);
        if (not depth)
        {
            return false;
        }
        m.check(*depth, "16UC1");
        if (s.max_rate > 0.0 and
            std::chrono::steady_clock::now() - m.last_enqueued < std::chrono::duration<double>(1.0 / s.max_rate))
        {
            std::lock_guard lock(m.mutex);
            ++m.stats.rate_limited;
            return true;
        }
        std::optional<Frame> ir = m.matching_ir(depth->stamp_ns());
        if (not ir)
        {
            std::lock_guard lock(m.mutex);
            ++m.stats.unpaired;
            return true;
        }
        m.check(*ir, "mono16");
        {
            std::lock_guard lock(m.mutex);
            ++m.stats.pairs;
        }
        k4a::image depth_image = to_k4a(*depth, K4A_IMAGE_FORMAT_DEPTH16);
        k4a::image ir_image = to_k4a(*ir, K4A_IMAGE_FORMAT_IR16);
        m.mask(depth_image, ir_image, s);
        const auto device = std::chrono::microseconds(depth->stamp_ns() / 1000);
        depth_image.set_timestamp(device);
        ir_image.set_timestamp(device);
        k4a::capture capture = k4a::capture::create();
        capture.set_depth_image(depth_image);
        capture.set_ir_image(ir_image);
        // the stamp is noted before the capture is enqueued: its result may come at once
        {
            std::lock_guard lock(m.mutex);
            m.stamps.emplace_back(static_cast<std::uint64_t>(device.count()), depth->stamp_ns());
            if (m.stamps.size() > 64)
            {
                m.stamps.pop_front();
            }
        }
        const bool enqueued = m.tracker.enqueue_capture(capture, std::chrono::milliseconds(0));
        std::lock_guard lock(m.mutex);
        if (enqueued)
        {
            ++m.stats.enqueued;
            m.last_enqueued = std::chrono::steady_clock::now();
        }
        else
        {
            ++m.stats.queue_full;
            m.stamps.pop_back();
        }
        return true;
    }

    BodyTrackingStats BodyTracking::stats() const
    {
        std::lock_guard lock(impl_->mutex);
        return impl_->stats;
    }

    void BodyTracking::set(const BodyTrackingSettings& settings)
    {
        if (settings.max_rate < 0.0)
        {
            throw std::invalid_argument("max_rate " + std::to_string(settings.max_rate) + ": 0 (every pair) or more");
        }
        std::lock_guard lock(impl_->mutex);
        impl_->settings = settings;
    }

    BodyTrackingSettings BodyTracking::settings() const
    {
        std::lock_guard lock(impl_->mutex);
        return impl_->settings;
    }

    DiagnosticStatus BodyTracking::report(const std::vector<KeyValue>& extra)
    {
        Impl& m = *impl_;
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::max(1e-3, std::chrono::duration<double>(now - m.reported).count());
        BodyTrackingStats a;
        BodyTrackingSettings settings;
        {
            std::lock_guard lock(m.mutex);
            a = m.stats;
            settings = m.settings;
        }
        const BodyTrackingStats& b = m.reported_stats;
        const auto number = [](double v) {
            char text[32];
            std::snprintf(text, sizeof(text), "%.2f", v);
            return std::string(text);
        };
        const std::uint64_t results = a.results - b.results;
        const double latency = results ? (a.latency_ms - b.latency_ms) / double(results) : 0.0;
        DiagnosticStatus status;
        status.name = "body_tracking";
        status.hardware_id = m.record.serial;
        status.values = {{"enabled", settings.enabled ? "1" : "0"},
                         {"processing", m.o.processing},
                         {"pairs/s", number(double(a.pairs - b.pairs) / seconds)},
                         {"results/s", number(double(results) / seconds)},
                         {"bodies", std::to_string(a.bodies - b.bodies)},
                         {"latency ms", number(latency)},
                         {"unpaired", std::to_string(a.unpaired - b.unpaired)},
                         {"tracker busy", std::to_string(a.queue_full - b.queue_full)},
                         {"rate-limited", std::to_string(a.rate_limited - b.rate_limited)},
                         {"max rate", number(settings.max_rate)}};
        status.values.insert(status.values.end(), extra.begin(), extra.end());
        if (not settings.enabled)
        {
            status.message = "off";
        }
        else if (a.pairs == b.pairs)
        {
            status.level = DiagnosticStatus::kError;
            status.message = "no depth + IR pairs from " + m.base;
        }
        else if (a.unpaired != b.unpaired or a.queue_full != b.queue_full)
        {
            status.level = DiagnosticStatus::kWarn;
            status.message = number(double(results) / seconds) + " results/s, " +
                             std::to_string(a.unpaired - b.unpaired) + " unpaired, " +
                             std::to_string(a.queue_full - b.queue_full) + " dropped (tracker busy)";
        }
        else
        {
            status.message = number(double(results) / seconds) + " results/s, latency " + number(latency) + " ms";
        }
        DiagnosticArray diagnostics;
        diagnostics.header.stamp = Time::from_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                     std::chrono::system_clock::now().time_since_epoch())
                                                     .count());
        diagnostics.status = {status};
        m.diagnostics->publish(diagnostics);
        m.reported = now;
        m.reported_stats = a;
        return status;
    }

    std::string default_tensorrt_cache()
    {
#ifdef XBLICK_K4A_ONNXRUNTIME_COMPAT
        if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr and *xdg != 0)
        {
            return std::string(xdg) + "/xblick-k4abt";
        }
        if (const char* home = std::getenv("HOME"); home != nullptr and *home != 0)
        {
            return std::string(home) + "/.cache/xblick-k4abt";
        }
        throw std::runtime_error("neither XDG_CACHE_HOME nor HOME is set: give the TensorRT engine cache explicitly");
#else
        return "";
#endif
    }

    std::string describe(const BodyTrackingStats& now, const BodyTrackingStats& before, double seconds)
    {
        const std::uint64_t results = now.results - before.results;
        char text[256];
        std::snprintf(text, sizeof(text),
                      "%.1f pairs/s, %.1f results/s, %llu bodies, latency %.1f ms; unpaired %llu, tracker busy %llu, "
                      "rate-limited %llu",
                      double(now.pairs - before.pairs) / seconds, double(results) / seconds,
                      static_cast<unsigned long long>(now.bodies - before.bodies),
                      results ? (now.latency_ms - before.latency_ms) / double(results) : 0.0,
                      static_cast<unsigned long long>(now.unpaired - before.unpaired),
                      static_cast<unsigned long long>(now.queue_full - before.queue_full),
                      static_cast<unsigned long long>(now.rate_limited - before.rate_limited));
        return text;
    }
}  // namespace xblick::k4a_node
