// Body tracking decoupled from the camera, without a camera: the node publishes captures of a rendered mannequin
// (capsules and a sphere, A-pose, 2.8 m in front of a wall, on a floor), the tracker in this process takes depth and IR
// over Durchblick and the calibration from the node's record, and its results arrive on <base>/bodies. Tested: the
// path (calibration record -> tracker, depth + IR paired by stamp -> capture -> tracker -> result with the frames'
// stamp) and that the tracker finds the mannequin where the SDK's own processing finds it. A tracker that fails
// silently (the SDK's CUDA 11 on a GPU it has no kernels for) delivers frames without bodies and fails here.
//
//   test_k4a_body_tracking MODEL [cpu|cuda|tensorrt [TENSORRT_CACHE_DIR]]
//   test_k4a_body_tracking --publish ENDPOINT BASE
//
// --publish: only the camera side, listening on ENDPOINT, until SIGINT/SIGTERM: the mannequin for a tracker in another
// process.
// MODEL: dnn_model_2_0_op11.onnx (the SDK's lite model does not find the mannequin, with the SDK's own processing
// neither).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <functional>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "body_tracking.hpp"
#include "xblick/camera.hpp"
#include "node.hpp"

namespace
{
    using namespace xblick;
    using namespace xblick::k4a_node;
    using namespace std::chrono_literals;

    std::string brown_conrady_calibration()
    {
        std::ifstream in(K4A_TEST_CALIBRATION, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        nlohmann::json j = nlohmann::json::parse(s.str());
        for (auto& camera : j.at("CalibrationInformation").at("Cameras"))
        {
            camera.at("Intrinsics").at("ModelType") = "CALIBRATION_LensDistortionModelBrownConrady";
            camera.at("Intrinsics").at("ModelParameters").at(10) = 0.0;
            camera.at("Intrinsics").at("ModelParameters").at(11) = 0.0;
        }
        return j.dump();
    }

    struct V
    {
        float x, y, z;
    };
    V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    V operator*(V a, float s) { return {a.x * s, a.y * s, a.z * s}; }
    float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    struct Capsule
    {
        V a, b;
        float r;
    };

    float distance(V p, const Capsule& c)
    {
        const V ab = c.b - c.a;
        const float t = std::clamp(dot(p - c.a, ab) / dot(ab, ab), 0.f, 1.f);
        const V d = p - (c.a + ab * t);
        return std::sqrt(dot(d, d)) - c.r;
    }

    // A person of about 1.75 m in an A-pose, 2.8 m in front of the depth camera, which is 1 m above the floor (the
    // depth camera's frame: x right, y down, z forward; metres).
    std::vector<Capsule> mannequin()
    {
        const float z = 2.8f;
        std::vector<Capsule> c = {{{-0.07f, 0.0f, z}, {-0.09f, -0.33f, z}, 0.12f},
                                  {{0.07f, 0.0f, z}, {0.09f, -0.33f, z}, 0.12f},
                                  {{0, -0.45f, z}, {0, -0.53f, z}, 0.05f},
                                  {{0, -0.63f, z}, {0, -0.66f, z}, 0.095f}};
        for (float s : {-1.f, 1.f})
        {
            const V chest{0, -0.33f, z}, shoulder{s * 0.2f, -0.42f, z}, elbow{s * 0.32f, -0.17f, z},
                wrist{s * 0.40f, 0.06f, z}, hand{s * 0.42f, 0.14f, z}, hip{s * 0.1f, 0.06f, z},
                knee{s * 0.11f, 0.52f, z}, ankle{s * 0.11f, 0.93f, z}, toe{s * 0.12f, 0.97f, z - 0.17f};
            for (const Capsule& part : {Capsule{chest, shoulder, 0.07f}, Capsule{shoulder, elbow, 0.05f},
                                        Capsule{elbow, wrist, 0.04f}, Capsule{wrist, hand, 0.035f},
                                        Capsule{hip, knee, 0.075f}, Capsule{knee, ankle, 0.055f},
                                        Capsule{ankle, toe, 0.04f}})
            {
                c.push_back(part);
            }
        }
        return c;
    }

    // The mannequin, the wall (3.8 m) and the floor as the depth camera sees them (sphere tracing along each pixel's
    // ray through the calibration's lens model): depth in mm, IR as reflectance / distance^2, with noise.
    void render(const k4a_calibration_t& calibration, std::vector<std::uint16_t>& depth, std::vector<std::uint16_t>& ir)
    {
        const std::vector<Capsule> parts = mannequin();
        depth.assign(640 * 576, 0);
        ir.assign(640 * 576, 0);
        std::mt19937 random(1);
        std::normal_distribution<float> noise(0.f, 1.f);
        for (int y = 0; y < 576; ++y)
        {
            for (int x = 0; x < 640; ++x)
            {
                const k4a_float2_t pixel{{float(x), float(y)}};
                k4a_float3_t ray;
                int valid = 0;
                k4a_calibration_2d_to_3d(&calibration, &pixel, 1000.f, K4A_CALIBRATION_TYPE_DEPTH,
                                         K4A_CALIBRATION_TYPE_DEPTH, &ray, &valid);
                if (not valid)
                {
                    continue;
                }
                const V direction = V{ray.xyz.x, ray.xyz.y, ray.xyz.z} * (1.f / 1000.f);  // z = 1
                float t = 0.3f;
                bool person = false;
                for (int step = 0; step < 200 and t < 8.f; ++step)
                {
                    const V p = direction * t;
                    float d = std::min(3.8f - p.z, 1.0f - p.y);
                    float body = 1e9f;
                    for (const Capsule& c : parts)
                    {
                        body = std::min(body, distance(p, c));
                    }
                    person = body < d;
                    d = std::min(d, body);
                    if (d < 0.0005f)
                    {
                        break;
                    }
                    t += d;
                }
                depth[y * 640 + x] = std::uint16_t(std::lround(t * 1000.f + noise(random) * 2.f));
                ir[y * 640 + x] = std::uint16_t(
                    std::clamp((person ? 900.f : 500.f) * 4.f / (t * t) + noise(random) * 10.f, 0.f, 65000.f));
            }
        }
    }

    k4a::capture capture(const std::vector<std::uint16_t>& depth_mm, const std::vector<std::uint16_t>& ir_values,
                         std::chrono::microseconds device, std::chrono::nanoseconds system)
    {
        k4a::capture capture = k4a::capture::create();
        k4a::image depth = k4a::image::create(K4A_IMAGE_FORMAT_DEPTH16, 640, 576, 640 * 2);
        k4a::image ir = k4a::image::create(K4A_IMAGE_FORMAT_IR16, 640, 576, 640 * 2);
        std::copy(depth_mm.begin(), depth_mm.end(), reinterpret_cast<std::uint16_t*>(depth.get_buffer()));
        std::copy(ir_values.begin(), ir_values.end(), reinterpret_cast<std::uint16_t*>(ir.get_buffer()));
        for (k4a::image* image : {&depth, &ir})
        {
            image->set_timestamp(device);
            k4a_image_set_system_timestamp_nsec(image->handle(), static_cast<std::uint64_t>(system.count()));
        }
        capture.set_depth_image(depth);
        capture.set_ir_image(ir);
        return capture;
    }

    // Where the SDK's own processing (its ONNX Runtime 1.10 on the CPU, dnn_model_2_0_op11) puts the mannequin's joints,
    // in mm (2026-10-27). ONNX Runtime 1.31 on the CPU and with CUDA without TF32 gave the same to 1 mm; with TF32 the
    // joints moved by up to 35 mm, hence the tolerance.
    struct Expected
    {
        const char* joint;
        double x, y, z;
    };
    constexpr Expected kExpected[] = {{"Pelvis", -1, 98, 2867},     {"Neck", 3, -433, 2769},
                                      {"Head", 2, -511, 2742},      {"WristLeft", 430, 62, 2795},
                                      {"WristRight", -396, 83, 2779}, {"AnkleLeft", 100, 872, 2812}};
    constexpr double kToleranceMm = 50;

    CalibrationRecord test_record()
    {
        return {.raw = brown_conrady_calibration(), .depth_mode = K4A_DEPTH_MODE_NFOV_UNBINNED,
                .color_resolution = K4A_COLOR_RESOLUTION_OFF, .fps = 30, .serial = "000000000000",
                .depth_frame_id = "AzureKinectDepthCamera", .color_frame_id = "AzureKinectCamera"};
    }

    // the camera at 30 fps until `done`; `sent` gets each capture's stamp
    void publish(Node& node, const std::vector<std::uint16_t>& depth, const std::vector<std::uint16_t>& ir,
                 const std::atomic<bool>& done, const std::function<void(std::int64_t)>& sent)
    {
        for (int seq = 0; not done; ++seq)
        {
            const auto device = std::chrono::microseconds(2000000 + seq * 33333);
            // arrival on the host's monotonic clock, as the SDK stamps it: the result's latency is then real
            const auto arrival =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
            node.publish(capture(depth, ir, device, arrival), true);
            sent(node.clock().to_wall_ns(device));
            std::this_thread::sleep_for(33ms);
        }
    }

    std::atomic<bool> stop{false};

    int publish_only(const std::string& endpoint, const std::string& base)
    {
        std::signal(SIGINT, [](int) { stop = true; });
        std::signal(SIGTERM, [](int) { stop = true; });
        zenoh::Session session = open_session({.listen = {endpoint}});
        const CalibrationRecord record = test_record();
        Node node(session, NodeOptions{.base = base, .source = "test", .depth_to_color = false}, record);
        std::vector<std::uint16_t> depth, ir;
        render(record.calibration(), depth, ir);
        std::fprintf(stderr, "publishing the mannequin on %s/{depth,ir} at %s\n", base.c_str(), endpoint.c_str());
        publish(node, depth, ir, stop, [](std::int64_t) {});
        return 0;
    }
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2 or (std::string(argv[1]) == "--publish" and argc != 4))
    {
        std::fprintf(stderr, "usage: test_k4a_body_tracking MODEL [cpu|cuda|tensorrt [TENSORRT_CACHE_DIR]]\n"
                             "       test_k4a_body_tracking --publish ENDPOINT BASE\n");
        return 2;
    }
    try
    {
        if (std::string(argv[1]) == "--publish")
        {
            return publish_only(argv[2], argv[3]);
        }
        std::random_device random;
        const int port = 20000 + int(random() % 20000);
        zenoh::Session camera_side = open_session({.listen = {"127.0.0.1:" + std::to_string(port)}});
        zenoh::Session tracker_side = open_session({.connect = {"127.0.0.1:" + std::to_string(port)}});
        const CalibrationRecord record = test_record();
        const std::string base = "t/k4abt";
        Node node(camera_side, NodeOptions{.base = base, .source = "test", .depth_to_color = false}, record);
        std::vector<std::uint16_t> depth, ir;
        render(record.calibration(), depth, ir);

        std::vector<std::string> messages;
        std::vector<std::string> attachments;  // each sample's (protocol.hpp): publish_ns against the result's stamp
        std::mutex mutex;
        auto listener = camera_side.declare_subscriber(
            zenoh::KeyExpr(base + "/bodies"),
            [&](const zenoh::Sample& sample) {
                std::lock_guard lock(mutex);
                messages.push_back(sample.get_payload().as_string());
                const auto attachment = sample.get_attachment();
                attachments.push_back(attachment ? attachment->get().as_string() : "");
            },
            zenoh::closures::none);

        std::vector<BodyResult> results;  // through on_result
        BodyTrackingOptions options{.base = base, .model_path = argv[1], .processing = argc > 2 ? argv[2] : "cpu",
                                    .tensorrt_cache = argc > 3 ? argv[3] : ""};
        options.on_result = [&](const BodyResult& result) {
            std::lock_guard lock(mutex);
            results.push_back(result);
        };
        const auto created = std::chrono::steady_clock::now();
        BodyTracking tracking(tracker_side, options);
        std::fprintf(stderr, "tracker created (%s) in %.1f s\n", options.processing.c_str(),
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - created).count());

        // the camera at 30 fps in a thread of its own, the tracker in this one, until kResults results came (the first
        // kWarmUp are left out of the latency: the first inferences are slower)
        constexpr std::size_t kResults = 30, kWarmUp = 10;
        std::set<std::int64_t> sent;
        std::atomic<bool> done{false};
        std::thread camera([&] {
            publish(node, depth, ir, done, [&](std::int64_t stamp) {
                std::lock_guard lock(mutex);
                sent.insert(stamp);
            });
        });
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < 120s)
        {
            tracking.step(200ms);
            std::lock_guard lock(mutex);
            if (messages.size() >= kResults)
            {
                break;
            }
        }

        // the settings while running (BodyTracking::set): off, then a mask over the whole image, then a rate limit;
        // the last phase is unmasked, so the checks below see the mannequin again
        const auto run_for = [&](std::chrono::milliseconds span) {
            const auto end = std::chrono::steady_clock::now() + span;
            while (std::chrono::steady_clock::now() < end)
            {
                tracking.step(100ms);
            }
        };
        const auto result_count = [&] {
            std::lock_guard lock(mutex);
            return results.size();
        };
        tracking.set({.enabled = false});
        run_for(1500ms);  // the pairs already in the tracker still give results
        const std::size_t off_before = result_count();
        run_for(1500ms);
        const std::size_t off_after = result_count();
        tracking.set({.enabled = true, .mask_min_x = 640});
        run_for(3000ms);
        std::size_t masked_results = 0, masked_bodies = 0;
        {
            std::lock_guard lock(mutex);
            masked_results = results.size() - off_after;
            masked_bodies = masked_results ? results.back().bodies.size() : 0;
        }
        tracking.set({.enabled = true, .max_rate = 5.0});
        run_for(1000ms);  // the queue drains
        const BodyTrackingStats limited_before = tracking.stats();
        run_for(3000ms);
        const BodyTrackingStats limited_after = tracking.stats();
        const double limited_rate = double(limited_after.enqueued - limited_before.enqueued) / 3.0;
        const DiagnosticStatus diagnosed = tracking.report({{"extra", "42"}});
        done = true;
        camera.join();

        int failures = 0;
        const auto expect = [&](bool condition, const std::string& what) {
            if (not condition)
            {
                std::fprintf(stderr, "FAILED: %s\n", what.c_str());
                ++failures;
            }
        };
        const BodyTrackingStats& s = tracking.stats();
        std::fprintf(stderr, "pairs %llu, unpaired %llu, enqueued %llu, tracker busy %llu, results %llu\n",
                     (unsigned long long)s.pairs, (unsigned long long)s.unpaired, (unsigned long long)s.enqueued,
                     (unsigned long long)s.queue_full, (unsigned long long)s.results);
        std::lock_guard lock(mutex);
        expect(messages.size() >= kResults, "results arrive on <base>/bodies");
        {
            // every result carries the attachment, sent after its capture: publish_ns >= the depth frame's stamp
            std::size_t sent_after = 0;
            for (std::size_t i = 0; i < messages.size(); ++i)
            {
                if (attachments[i].empty())
                {
                    continue;
                }
                const nlohmann::json a = nlohmann::json::parse(attachments[i]);
                const std::int64_t stamp = nlohmann::json::parse(messages[i]).at("stamp_ns").get<std::int64_t>();
                sent_after += (a.at("v") == 1 and a.at("publish_ns").get<std::int64_t>() >= stamp) ? 1 : 0;
            }
            expect(sent_after == messages.size(), "each bodies sample has the attachment, publish_ns after its stamp");
        }
        expect(s.pairs > 0 and s.enqueued > 0, "depth and IR are paired and given to the tracker");
        if (not messages.empty())
        {
            const nlohmann::json m = nlohmann::json::parse(messages.back());
            expect(m.at("v") == kBodiesVersion and m.at("model") == "K4ABT_BODY_32" and
                       m.at("frame_id") == "AzureKinectDepthCamera",
                   "the result's header");
            expect(sent.count(m.at("stamp_ns").get<std::int64_t>()) == 1, "the result carries its frames' stamp");
            std::vector<double> latencies;
            for (std::size_t i = kWarmUp; i < messages.size(); ++i)
            {
                latencies.push_back(nlohmann::json::parse(messages[i]).at("latency_ms").get<double>());
            }
            std::sort(latencies.begin(), latencies.end());
            if (not latencies.empty())
            {
                std::fprintf(stderr, "latency (capture to result, results %zu to %zu): median %.1f ms, max %.1f ms\n",
                             kWarmUp + 1, messages.size(), latencies[latencies.size() / 2], latencies.back());
            }
            expect(not latencies.empty() and latencies.front() > 0 and latencies.back() < 60000,
                   "the latency is capture to result on the host's clock");
            expect(m.at("bodies").size() == 1, "the mannequin is found, as one body");
            // on_result is called after the message went out: it has every result up to this one
            const auto same = std::find_if(results.begin(), results.end(), [&](const BodyResult& r) {
                return r.stamp_ns == m.at("stamp_ns").get<std::int64_t>();
            });
            expect(same != results.end() and same->frame_id == m.at("frame_id").get<std::string>() and
                       same->bodies.size() == m.at("bodies").size() and
                       (same->bodies.empty() or
                        same->bodies[0].joints[0].position_mm[2] ==
                            m.at("bodies")[0].at("joints").at(kJointNames[0]).at("position_mm")[2].get<float>()),
                   "on_result gets the result that went out on <base>/bodies");
            if (m.at("bodies").size() == 1)
            {
                const nlohmann::json& joints = m.at("bodies")[0].at("joints");
                for (const Expected& e : kExpected)
                {
                    const auto& p = joints.at(e.joint).at("position_mm");
                    const double error = std::hypot(p[0].get<double>() - e.x, p[1].get<double>() - e.y,
                                                    p[2].get<double>() - e.z);
                    std::fprintf(stderr, "%-10s (%6.0f %6.0f %6.0f) mm, %4.1f mm from the SDK's own\n", e.joint,
                                 p[0].get<double>(), p[1].get<double>(), p[2].get<double>(), error);
                    expect(error < kToleranceMm, std::string(e.joint) + " where the SDK's own processing puts it");
                }
            }
        }
        std::fprintf(stderr, "off: %zu -> %zu results; masked: %zu results, the last with %zu bodies; limited to 5/s: "
                             "%.1f pairs/s tracked, %llu skipped\n",
                     off_before, off_after, masked_results, masked_bodies, limited_rate,
                     (unsigned long long)(limited_after.rate_limited - limited_before.rate_limited));
        expect(off_after == off_before, "off: no results");
        expect(masked_results > 0 and masked_bodies == 0, "on again with a mask over the whole image: results, no bodies");
        expect(limited_rate > 2.0 and limited_rate < 6.5 and limited_after.rate_limited > limited_before.rate_limited,
               "max_rate 5: about 5 pairs/s tracked, the others skipped");
        const auto value = [&](const std::string& key) {
            for (const KeyValue& kv : diagnosed.values)
            {
                if (kv.key == key)
                {
                    return kv.value;
                }
            }
            return std::string("(missing)");
        };
        std::fprintf(stderr, "diagnostics: %s (level %d)\n", diagnosed.message.c_str(), int(diagnosed.level));
        expect(diagnosed.name == "body_tracking" and diagnosed.level != DiagnosticStatus::kError and
                   std::stod(value("pairs/s")) > 0 and std::stod(value("rate-limited")) > 0 and
                   value("max rate") == "5.00" and value("extra") == "42",
               "report(): the status body_tracking with its rates, settings and the extra values");
        std::fprintf(stderr, failures ? "%d failures\n" : "ok\n", failures);
        return failures ? 1 : 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "FAILED with an exception: %s\n", e.what());
        return 1;
    }
}
