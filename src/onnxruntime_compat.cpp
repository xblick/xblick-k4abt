// The Body Tracking SDK 1.1 (libk4abt.so) with a current ONNX Runtime instead of its bundled 1.10 (CUDA 11, cuDNN 8),
// which has no kernels for GPUs of compute capability 10 and newer (Blackwell): every inference fails there, and the
// SDK returns frames without bodies. libk4abt needs libonnxruntime.so.1.10.0 and imports three functions with the
// symbol version VERS_1.10.0 (nm -D --undefined-only): OrtGetApiBase and the two legacy functions that append the CUDA
// and the TensorRT execution provider. This library has that soname and that symbol version and forwards the three
// calls to the ONNX Runtime given at build time (XBLICK_K4A_ONNXRUNTIME_DIR, its libonnxruntime.so.1). ONNX
// Runtime's C API keeps older API versions (OrtGetApiBase()->GetApi(n) for n up to the current one), so libk4abt gets
// the function table it was built against. Loaded before libk4abt (xblick-k4abt links it), the dynamic linker
// takes it for libk4abt's libonnxruntime.so.1.10.0 by its soname.
//
// One call is not forwarded: ReleaseEnv. libk4abt keeps its OrtEnv in a static object and releases it from that
// object's destructor, at process exit. With ONNX Runtime 1.31 and the CUDA execution provider that release corrupts
// the heap (measured 2026-10-27: "corrupted double-linked list" after ReleaseEnv, in 3 of 3 runs, also with no frame
// tracked; none with the release left out, also under glibc's MALLOC_CHECK_=3 while tracking). libk4abt's trace of API
// calls shows ReleaseEnv only there, after the last ReleaseSession, so leaving it out leaks nothing before the exit.
//
// The CUDA execution provider is appended with TF32 off (use_tf32 = 0; on by default in current versions). With TF32
// the joints of a rendered mannequin moved by 20 to 35 mm against the SDK's own CPU processing; without it they agree to
// the millimetre (2026-10-27, RTX PRO 6000, dnn_model_2_0_op11).
//
// TensorRT uses TF32 as well (BuilderFlag::kTF32 is on by default), with the same effect (up to 96 mm), and ONNX
// Runtime has no provider option for it; TensorRT turns it off when it builds the engine with NVIDIA_TF32_OVERRIDE=0
// in the environment, which the TensorRT execution provider is appended with. The variable also turns TF32 off in
// cuBLAS and cuDNN of the whole process.
//
// TensorRT builds its engine for the GPU at every start (10 s for dnn_model_2_0_op11 on the RTX PRO 6000) unless it
// is cached: xblick_onnxruntime_tensorrt_cache(dir), called before the tracker is created, has the TensorRT
// execution provider appended with ONNX Runtime's engine and timing cache in dir.
#include <dlfcn.h>
#include <onnxruntime_c_api.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    void* real(const char* name)
    {
        static void* const library = dlopen("libonnxruntime.so.1", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr)
        {
            std::fprintf(stderr, "xblick-k4abt: ONNX Runtime: %s\n", dlerror());
            std::abort();
        }
        void* f = dlsym(library, name);
        if (f == nullptr)
        {
            std::fprintf(stderr, "xblick-k4abt: ONNX Runtime has no %s\n", name);
            std::abort();
        }
        return f;
    }

    const OrtApiBase* real_base()
    {
        static const OrtApiBase* const base = reinterpret_cast<const OrtApiBase* (*)()>(real("OrtGetApiBase"))();
        return base;
    }

    // ONNX Runtime's function table (the same for every API version it serves), ReleaseEnv left out (see above)
    const OrtApi* get_api(std::uint32_t version) noexcept
    {
        const OrtApi* api = real_base()->GetApi(version);
        if (api == nullptr)
        {
            return nullptr;
        }
        static const OrtApi patched = [api] {
            OrtApi copy = *api;
            copy.ReleaseEnv = [](OrtEnv*) noexcept {};
            return copy;
        }();
        return &patched;
    }

    const char* version_string() noexcept
    {
        return real_base()->GetVersionString();
    }

    const OrtApiBase base{get_api, version_string};

    std::string tensorrt_cache;  // empty: no engine cache
}  // namespace

extern "C"
{
    __attribute__((visibility("default"))) const OrtApiBase* OrtGetApiBase() noexcept
    {
        return &base;
    }

    __attribute__((visibility("default"))) OrtStatus* OrtSessionOptionsAppendExecutionProvider_CUDA(
        OrtSessionOptions* options, int device_id) noexcept
    {
        const OrtApi* api = real_base()->GetApi(ORT_API_VERSION);
        OrtCUDAProviderOptionsV2* cuda = nullptr;
        if (OrtStatus* status = api->CreateCUDAProviderOptions(&cuda))
        {
            return status;
        }
        const std::string device = std::to_string(device_id);
        const char* keys[] = {"device_id", "use_tf32"};
        const char* values[] = {device.c_str(), "0"};
        OrtStatus* status = api->UpdateCUDAProviderOptions(cuda, keys, values, 2);
        if (status == nullptr)
        {
            status = api->SessionOptionsAppendExecutionProvider_CUDA_V2(options, cuda);
        }
        api->ReleaseCUDAProviderOptions(cuda);
        return status;
    }

    __attribute__((visibility("default"))) OrtStatus* OrtSessionOptionsAppendExecutionProvider_Tensorrt(
        OrtSessionOptions* options, int device_id) noexcept
    {
        static const auto f = reinterpret_cast<OrtStatus* (*)(OrtSessionOptions*, int)>(
            real("OrtSessionOptionsAppendExecutionProvider_Tensorrt"));
        if (const char* tf32 = std::getenv("NVIDIA_TF32_OVERRIDE"); tf32 != nullptr and std::string(tf32) != "0")
        {
            std::fprintf(stderr, "xblick-k4abt: NVIDIA_TF32_OVERRIDE=%s; TensorRT body tracking needs it 0\n", tf32);
            std::abort();
        }
        setenv("NVIDIA_TF32_OVERRIDE", "0", 1);
        if (tensorrt_cache.empty())
        {
            return f(options, device_id);
        }
        const OrtApi* api = real_base()->GetApi(ORT_API_VERSION);
        OrtTensorRTProviderOptionsV2* tensorrt = nullptr;
        if (OrtStatus* status = api->CreateTensorRTProviderOptions(&tensorrt))
        {
            return status;
        }
        const std::string device = std::to_string(device_id);
        const char* keys[] = {"device_id", "trt_engine_cache_enable", "trt_engine_cache_path",
                              "trt_timing_cache_enable", "trt_timing_cache_path"};
        const char* values[] = {device.c_str(), "1", tensorrt_cache.c_str(), "1", tensorrt_cache.c_str()};
        OrtStatus* status = api->UpdateTensorRTProviderOptions(tensorrt, keys, values, 5);
        if (status == nullptr)
        {
            status = api->SessionOptionsAppendExecutionProvider_TensorRT_V2(options, tensorrt);
        }
        api->ReleaseTensorRTProviderOptions(tensorrt);
        return status;
    }

    // the directory of TensorRT's engine and timing cache (it must exist); before the tracker is created
    __attribute__((visibility("default"))) void xblick_onnxruntime_tensorrt_cache(const char* dir) noexcept
    {
        tensorrt_cache = dir;
    }
}
