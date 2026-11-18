#!/bin/bash
# Sets xblick-k4abt up from scratch or updates it: the prerequisites, the build, the tests, the installation.
#
#     tools/setup.sh [--cuda-toolkit DIR]
#
# * Prerequisites (pinned versions, each download checked against its sha256) in the node's cache directory of
#   Durchblick, cache_dir("k4abt"): $XBLICK_HOME/cache/k4abt, else $XDG_CACHE_HOME/xblick/k4abt, else
#   ~/.cache/xblick/k4abt. Downloads are kept in its downloads/; a file there with the right checksum is not
#   downloaded again, so a file obtained elsewhere can be put there. The tracker's TensorRT engine cache is in the same
#   directory, in subdirectories onnxruntime-<version>_tensorrt-<version>.
# * The build in build-setup/, the test (ctest, on the GPU with TensorRT), the installation in install/ with
#   install/env.sh, which puts the programs on PATH and names the SDK's model.
# * Microsoft's Body Tracking SDK and Sensor SDK, ONNX Runtime, cuDNN and TensorRT: about 11 GB of downloads with
#   the CUDA 12 builds. They need a CUDA toolkit at /usr/local/cuda or at --cuda-toolkit DIR, 12 or 13: the script
#   takes the builds of ONNX Runtime, cuDNN and TensorRT for the toolkit's major version.
#
# It never asks and never uses sudo. What needs root it names with the command, then stops. Run again, it does only
# what changed.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/setup.sh [--cuda-toolkit DIR]

Installs the prerequisites into the cache directory of xblick (cache/k4abt below $XBLICK_HOME, else
$XDG_CACHE_HOME/xblick/k4abt, else ~/.cache/xblick/k4abt), builds the tracker in build-setup/, runs its test and
installs it in install/. Afterwards: source install/env.sh.

  --cuda-toolkit DIR  the CUDA toolkit, 12 or 13 (default /usr/local/cuda); the script takes the builds of ONNX
                      Runtime 1.31.1, cuDNN 9.15 and TensorRT 10.14 for its major version (about 11 GB of downloads
                      with the CUDA 12 builds and the Body Tracking SDK 1.1.2)
  -h, --help          this text
EOF
}

die() {
    echo "setup.sh: $*" >&2
    exit 1
}

toolkit=/usr/local/cuda
while (($#)); do
    case $1 in
        --cuda-toolkit) (($# > 1)) || die "--cuda-toolkit needs a directory"; toolkit=$2; shift ;;
        -h | --help) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
    shift
done

repo=$(cd "$(dirname "$0")/.." && pwd)

# ------------------------------------------------------------------------------------------------- pinned versions

zenoh_version=1.10.1
xblick_version=0.5.14
json_version=3.12.0
k4a_version=1.4.1
k4abt_version=1.1.2
ort_version=1.31.1
cudnn_version=9.15.0.57
cudnn13_version=9.15.0.58
tensorrt_version=10.14.1.48

# file name in downloads/ | URL | sha256
pins=(
    "zenoh-c-$zenoh_version-x86_64-unknown-linux-gnu-standalone.zip|https://github.com/eclipse-zenoh/zenoh-c/releases/download/$zenoh_version/zenoh-c-$zenoh_version-x86_64-unknown-linux-gnu-standalone.zip|9ee0f2d732b0f3042a7e1cd3076042a2bc3ac0415587c40bc3ed7b8b62fbde11"
    "zenoh-cpp-$zenoh_version.tar.gz|https://github.com/eclipse-zenoh/zenoh-cpp/archive/refs/tags/$zenoh_version.tar.gz|64d3c315c4fdfa54b4d96ee27ee2b3d98320b0fb289402fff9d7a99bc513528e"
    "zenoh-$zenoh_version-x86_64-unknown-linux-gnu-standalone.zip|https://github.com/eclipse-zenoh/zenoh/releases/download/$zenoh_version/zenoh-$zenoh_version-x86_64-unknown-linux-gnu-standalone.zip|512ac81438fd995ca2db02f75dd83afa048b6a11630ee10e0cb4dc423c39a5e9"
    "xblick-$xblick_version.tar.gz|https://github.com/xblick/xblick/archive/refs/tags/v$xblick_version.tar.gz|176652b48c71501fb1bd1e965b4d0dbf3b7331545f46b8cfb6d46311e447c186"
    "nlohmann-json-$json_version.tar.xz|https://github.com/nlohmann/json/releases/download/v$json_version/json.tar.xz|42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa"
    "libk4a1.4_${k4a_version}_amd64.deb|https://packages.microsoft.com/ubuntu/18.04/prod/pool/main/libk/libk4a1.4/libk4a1.4_${k4a_version}_amd64.deb|c1c63f81641eed1326136a44e5a5fd229e1e6315b2b20b2dddb5523a556c9329"
    "libk4a1.4-dev_${k4a_version}_amd64.deb|https://packages.microsoft.com/ubuntu/18.04/prod/pool/main/libk/libk4a1.4-dev/libk4a1.4-dev_${k4a_version}_amd64.deb|08303094b9ad36ea74c19bc8b8950c97055e73dd2e8bd18e2af5e165a2289cd2"
    "libk4abt1.1_${k4abt_version}_amd64.deb|https://packages.microsoft.com/ubuntu/18.04/prod/pool/main/libk/libk4abt1.1/libk4abt1.1_${k4abt_version}_amd64.deb|0a507ffbe27b49127d53c265b9ef259d0fd993c30765fd612a1ef4054985dc50"
    "libk4abt1.1-dev_${k4abt_version}_amd64.deb|https://packages.microsoft.com/ubuntu/18.04/prod/pool/main/libk/libk4abt1.1-dev/libk4abt1.1-dev_${k4abt_version}_amd64.deb|e04028542d35fcd7d3db2343227afc8e44f3bdc856dbe39eda0f14c31841d55b"
    # the builds for CUDA 12
    "onnxruntime-linux-x64-gpu_cuda12-$ort_version.tgz|https://github.com/microsoft/onnxruntime/releases/download/v$ort_version/onnxruntime-linux-x64-gpu_cuda12-$ort_version.tgz|57cd51f677eca19a269cdba31b0c16c15d73a8feea41f170aa8b750447ab1f70"
    "cudnn-linux-x86_64-${cudnn_version}_cuda12-archive.tar.xz|https://developer.download.nvidia.com/compute/cudnn/redist/cudnn/linux-x86_64/cudnn-linux-x86_64-${cudnn_version}_cuda12-archive.tar.xz|183cb46b7b6d5d7c70258ce674cf795611a59a32c220d25e4f772f9f07f77e29"
    "TensorRT-$tensorrt_version.Linux.x86_64-gnu.cuda-12.9.tar.gz|https://developer.download.nvidia.com/compute/machine-learning/tensorrt/10.14.1/tars/TensorRT-$tensorrt_version.Linux.x86_64-gnu.cuda-12.9.tar.gz|0daa7d5929c78edfbe86b474064d0f82d2064c475cc6be747c5101f1ccc37105"
    # the builds for CUDA 13
    "onnxruntime-linux-x64-gpu_cuda13-$ort_version.tgz|https://github.com/microsoft/onnxruntime/releases/download/v$ort_version/onnxruntime-linux-x64-gpu_cuda13-$ort_version.tgz|b2146862e691f7e338f5aff9c7a9a5567c2ca57cf01442a1adb3a13bf62d9095"
    "cudnn-linux-x86_64-${cudnn13_version}_cuda13-archive.tar.xz|https://developer.download.nvidia.com/compute/cudnn/redist/cudnn/linux-x86_64/cudnn-linux-x86_64-${cudnn13_version}_cuda13-archive.tar.xz|6ddfeaa00ba534c7917f15666d7fc1e8f05f822a7681eb9adbe4b86beaebe8bc"
    "TensorRT-$tensorrt_version.Linux.x86_64-gnu.cuda-13.0.tar.gz|https://developer.download.nvidia.com/compute/machine-learning/tensorrt/10.14.1/tars/TensorRT-$tensorrt_version.Linux.x86_64-gnu.cuda-13.0.tar.gz|c74af67db57f1a0d7e66bb01ab93f1ecda5facac491ca76e680d832f1e035ce6"
)

# ------------------------------------------------------------------------------------------------- the system

[[ $(uname -m) == x86_64 ]] || die "the SDKs exist for x86-64 only, this is $(uname -m)"
command -v dpkg-query > /dev/null || die "this script needs dpkg (Ubuntu or Debian)"

# the Ubuntu packages: the build tools, libjpeg and libpng (Durchblick); libk4a needs libudev, and it loads the depth
# engine at its start, which needs libGL and libX11 (the tracker does not use the depth engine, but libk4a logs errors
# without it); the Body Tracking SDK needs libgomp
packages=(cmake ninja-build g++ curl unzip xz-utils libjpeg-turbo8-dev libpng-dev libgl1 libx11-6 libudev1 libgomp1)
missing=()
for package in "${packages[@]}"; do
    # one line per architecture: with multiarch (i386 next to amd64) a package is listed twice
    dpkg-query -W -f='${Architecture} ${db:Status-Status}\n' "$package" 2> /dev/null |
        grep -Eqx '(amd64|all) installed' || missing+=("$package")
done
((${#missing[@]} == 0)) || die "missing system packages: sudo apt install ${missing[*]}"

cmake_version=$(cmake --version | sed -n 's/^cmake version \([0-9.]*\).*/\1/p')
[[ $(printf '%s\n' 3.24 "$cmake_version" | sort -V | head -1) == 3.24 ]] ||
    die "CMake $cmake_version is too old, Durchblick needs 3.24 or newer: sudo snap install cmake --classic"

[[ -f $toolkit/version.json && -x $toolkit/bin/nvcc ]] ||
    die "no CUDA toolkit at $toolkit: sudo apt install cuda-toolkit-12-8 (NVIDIA's repository," \
        "https://developer.nvidia.com/cuda-12-8-1-download-archive), or --cuda-toolkit DIR"
toolkit_version=$(sed -n '/"cuda"/,/}/s/.*"version" *: *"\([0-9.]*\)".*/\1/p' "$toolkit/version.json" | head -1)
toolkit_major=${toolkit_version%%.*}
[[ $toolkit_major =~ ^[0-9]+$ ]] || die "cannot read the CUDA version from $toolkit/version.json"
# the pinned ONNX Runtime, cuDNN and TensorRT are builds for one CUDA major version and load its libraries by name
((toolkit_major == 12 || toolkit_major == 13)) ||
    die "the CUDA toolkit at $toolkit is $toolkit_version, but body tracking needs CUDA 12 or 13 (ONNX Runtime," \
        "cuDNN and TensorRT are pinned as builds for these): --cuda-toolkit DIR"
for file in include/cuda.h "lib64/libcublas.so.$toolkit_major"; do
    [[ -e $toolkit/$file ]] || die "the CUDA toolkit at $toolkit has no $file"
done

# ------------------------------------------------------------------------------------------------- the cache

if [[ -n ${XBLICK_HOME:-} ]]; then
    [[ $XBLICK_HOME == /* ]] || die "XBLICK_HOME must be an absolute path: $XBLICK_HOME"
    cache=$XBLICK_HOME/cache/k4abt
else
    xdg=${XDG_CACHE_HOME:-}
    [[ $xdg != "~"* ]] || xdg=$HOME${xdg#\~}
    if [[ $xdg == /* ]]; then
        cache=$xdg/xblick/k4abt
    else
        cache=$HOME/.cache/xblick/k4abt
    fi
fi
downloads=$cache/downloads
mkdir -p "$downloads"
echo "prerequisites in $cache"

# fetch FILE [NOTICE]: FILE in downloads/ as pinned, downloaded if missing; NOTICE is printed when the download starts
fetch() {
    local name=$1 notice=${2:-} entry url sha256 file
    for entry in "${pins[@]}"; do
        [[ ${entry%%|*} == "$name" ]] && break
        entry=
    done
    [[ -n $entry ]] || die "internal: no pin for $name"
    url=${entry#*|}
    url=${url%|*}
    sha256=${entry##*|}
    file=$downloads/$name
    if [[ ! -f $file ]]; then
        [[ -z $notice ]] || echo "$notice"
        echo "downloading $url"
        local progress=(-sS)
        [[ ! -t 2 ]] || progress=(--progress-bar)
        curl -fL --retry 3 "${progress[@]}" -o "$file.part" "$url" || die "download failed: $url"
        mv "$file.part" "$file"
    fi
    if ! echo "$sha256  $file" | sha256sum --check --quiet --status; then
        die "$file does not have the pinned sha256 $sha256: delete it and run again"
    fi
}

# finished DIR: whether the step that fills DIR has finished (its stamp .setup-done exists)
finished() { [[ -f $1/.setup-done ]]; }
mark_finished() { touch "$1/.setup-done"; }

# notices for the binaries that come under their makers' terms
notice_k4a="Microsoft's Azure Kinect Sensor SDK $k4a_version binaries (libk4a, libk4arecord, the depth engine) are\
 under the Microsoft Software License Terms of the Azure Kinect Sensor SDK:\
 https://www.nuget.org/packages/Microsoft.Azure.Kinect.Sensor/$k4a_version/License"
notice_k4abt="Microsoft's Azure Kinect Body Tracking SDK $k4abt_version (with its models) is under the Microsoft\
 Software License Terms of the Azure Kinect Body Tracking SDK:\
 https://www.nuget.org/packages/Microsoft.Azure.Kinect.BodyTracking/$k4abt_version/License"
notice_cudnn="NVIDIA cuDNN is under the NVIDIA Software License Agreement for cuDNN:\
 https://docs.nvidia.com/deeplearning/cudnn/latest/reference/eula.html"
notice_tensorrt="NVIDIA TensorRT $tensorrt_version is under the NVIDIA TensorRT Software License Agreement:\
 https://docs.nvidia.com/deeplearning/tensorrt/latest/reference/sla.html"

# ------------------------------------------------------------------------------------------------- Zenoh

zenoh=$cache/zenoh-$zenoh_version
if ! finished "$zenoh"; then
    fetch "zenoh-c-$zenoh_version-x86_64-unknown-linux-gnu-standalone.zip"
    fetch "zenoh-cpp-$zenoh_version.tar.gz"
    fetch "zenoh-$zenoh_version-x86_64-unknown-linux-gnu-standalone.zip"
    echo "installing zenoh-c, zenoh-cpp and zenohd $zenoh_version"
    mkdir -p "$zenoh/bin" "$cache/src"
    unzip -qo "$downloads/zenoh-c-$zenoh_version-x86_64-unknown-linux-gnu-standalone.zip" -d "$zenoh"
    # the router zenohd, without its plugins
    unzip -qo -j "$downloads/zenoh-$zenoh_version-x86_64-unknown-linux-gnu-standalone.zip" zenohd -d "$zenoh/bin"
    tar -xzf "$downloads/zenoh-cpp-$zenoh_version.tar.gz" -C "$cache/src"
    cmake -S "$cache/src/zenoh-cpp-$zenoh_version" -B "$cache/build/zenoh-cpp-$zenoh_version" \
        -DCMAKE_INSTALL_PREFIX="$zenoh" -DCMAKE_PREFIX_PATH="$zenoh" -DZENOHCXX_ZENOHPICO=OFF > /dev/null
    cmake --install "$cache/build/zenoh-cpp-$zenoh_version" > /dev/null
    mark_finished "$zenoh"
fi

# ------------------------------------------------------------------------------------------------- nlohmann_json

json=$cache/nlohmann-json-$json_version
if ! finished "$json"; then
    fetch "nlohmann-json-$json_version.tar.xz"
    echo "installing nlohmann_json $json_version"
    mkdir -p "$json" "$cache/src/nlohmann-json-$json_version"
    tar -xJf "$downloads/nlohmann-json-$json_version.tar.xz" -C "$cache/src/nlohmann-json-$json_version"
    cmake -S "$cache/src/nlohmann-json-$json_version/json" -B "$cache/build/nlohmann-json-$json_version" -G Ninja \
        -DCMAKE_INSTALL_PREFIX="$json" -DJSON_BuildTests=OFF > /dev/null
    cmake --install "$cache/build/nlohmann-json-$json_version" > /dev/null
    mark_finished "$json"
fi

# ------------------------------------------------------------------------------------------------- Durchblick

# The core's C++ package, without its point cloud node, with cuda.h from the toolkit.
xblick=$cache/xblick-$xblick_version
if ! finished "$xblick"; then
    fetch "xblick-$xblick_version.tar.gz"
    tar -xzf "$downloads/xblick-$xblick_version.tar.gz" -C "$cache/src"
    echo "building Durchblick $xblick_version (C++) into $xblick"
    cmake -S "$cache/src/xblick-$xblick_version" -B "$cache/build/${xblick##*/}" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$xblick" -DCMAKE_PREFIX_PATH="$zenoh" \
        -DBUILD_TESTING=OFF -DXBLICK_POINTS=OFF -DXBLICK_CUDA_INCLUDE_DIR="$toolkit/include" > /dev/null
    cmake --build "$cache/build/${xblick##*/}" > /dev/null
    cmake --install "$cache/build/${xblick##*/}" > /dev/null
    mark_finished "$xblick"
fi

# ------------------------------------------------------------------------------------------------- the SDKs

# Microsoft's packages unpacked as they are (usr/...): libk4a finds the depth engine in usr/lib/x86_64-linux-gnu/
# libk4a1.4 through its own RUNPATH ($ORIGIN/libk4a1.4). The tracker never calls the depth engine; it is installed
# only because libk4a looks for it when it loads and otherwise logs error lines at start.
k4a=$cache/k4a-$k4a_version
if ! finished "$k4a"; then
    fetch "libk4a1.4_${k4a_version}_amd64.deb" "$notice_k4a"
    fetch "libk4a1.4-dev_${k4a_version}_amd64.deb"
    echo "unpacking the Azure Kinect Sensor SDK $k4a_version"
    mkdir -p "$k4a"
    dpkg-deb -x "$downloads/libk4a1.4_${k4a_version}_amd64.deb" "$k4a"
    dpkg-deb -x "$downloads/libk4a1.4-dev_${k4a_version}_amd64.deb" "$k4a"
    mark_finished "$k4a"
fi

k4abt=$cache/k4abt-$k4abt_version
if ! finished "$k4abt"; then
    fetch "libk4abt1.1_${k4abt_version}_amd64.deb" "$notice_k4abt"
    fetch "libk4abt1.1-dev_${k4abt_version}_amd64.deb"
    echo "unpacking the Azure Kinect Body Tracking SDK $k4abt_version (3.3 GB)"
    mkdir -p "$k4abt"
    dpkg-deb -x "$downloads/libk4abt1.1_${k4abt_version}_amd64.deb" "$k4abt"
    dpkg-deb -x "$downloads/libk4abt1.1-dev_${k4abt_version}_amd64.deb" "$k4abt"
    mark_finished "$k4abt"
fi

# ------------------------------------------------------------------------------------------------- ONNX Runtime, cuDNN, TensorRT

# the set of builds for the toolkit's CUDA major version
if ((toolkit_major == 13)); then
    cudnn_set_version=$cudnn13_version tensorrt_cuda=13.0 tensorrt_root=$cache/cuda13
else
    cudnn_set_version=$cudnn_version tensorrt_cuda=12.9 tensorrt_root=$cache
fi
ort_name=onnxruntime-linux-x64-gpu_cuda$toolkit_major-$ort_version
cudnn_name=cudnn-linux-x86_64-${cudnn_set_version}_cuda$toolkit_major-archive
tensorrt_name=TensorRT-$tensorrt_version.Linux.x86_64-gnu.cuda-$tensorrt_cuda
ort=$cache/$ort_name
if ! finished "$ort"; then
    fetch "$ort_name.tgz"
    echo "unpacking ONNX Runtime $ort_version for CUDA $toolkit_major"
    tar -xzf "$downloads/$ort_name.tgz" -C "$cache"
    mark_finished "$ort"
fi
cudnn=$cache/$cudnn_name
if ! finished "$cudnn"; then
    fetch "$cudnn_name.tar.xz" "$notice_cudnn"
    echo "unpacking cuDNN $cudnn_set_version"
    tar -xJf "$downloads/$cudnn_name.tar.xz" -C "$cache" --exclude='*_static*.a'
    mark_finished "$cudnn"
fi
tensorrt=$tensorrt_root/TensorRT-$tensorrt_version
if ! finished "$tensorrt"; then
    fetch "$tensorrt_name.tar.gz" "$notice_tensorrt"
    echo "unpacking TensorRT $tensorrt_version for CUDA $tensorrt_cuda (its libraries)"
    mkdir -p "$tensorrt_root"
    tar -xzf "$downloads/$tensorrt_name.tar.gz" -C "$tensorrt_root" \
        --exclude='*.a' --exclude='*_win_*' "TensorRT-$tensorrt_version/lib" "TensorRT-$tensorrt_version/doc"
    mark_finished "$tensorrt"
fi

# ------------------------------------------------------------------------------------------------- the tracker

build=$repo/build-setup
install=$repo/install
# every package by its directory: a directory cached by an earlier call with other options would win over the
# prefix path
configure=(cmake -S "$repo" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$install"
           -DCMAKE_PREFIX_PATH="$xblick;$zenoh;$json;$k4a/usr" -Dxblick_DIR="$xblick/lib/cmake/xblick"
           -Dzenohc_DIR="$zenoh/lib/cmake/zenohc" -Dzenohcxx_DIR="$zenoh/lib/cmake/zenohcxx"
           -Dnlohmann_json_DIR="$json/share/cmake/nlohmann_json" -Dk4a_DIR="$k4a/usr/lib/x86_64-linux-gnu/cmake/k4a"
           -Dk4abt_DIR="$k4abt/usr/lib/cmake/k4abt" -DCUDAToolkit_ROOT="$toolkit"
           -DXBLICK_K4A_ONNXRUNTIME_DIR="$ort" -DXBLICK_K4A_CUDNN_DIR="$cudnn" -DXBLICK_K4A_TENSORRT_DIR="$tensorrt"
           -DK4ABT_TEST_MODEL="$k4abt/usr/bin/dnn_model_2_0_op11.onnx" -DK4ABT_TEST_PROCESSING=tensorrt)
mkdir -p "$build"
if [[ ! -f $build/CMakeCache.txt || "$(cat "$build/.setup-configure" 2> /dev/null)" != "${configure[*]}" ]]; then
    echo "configuring the tracker in $build"
    rm -f "$build/.setup-tests-passed"
    "${configure[@]}" > "$build/.setup-configure.log" || die "configuring failed: $build/.setup-configure.log"
    echo "${configure[*]}" > "$build/.setup-configure"
fi
if [[ $(ninja -C "$build" -n 2>&1 | tail -1) != "ninja: no work to do." ]]; then
    echo "building the tracker"
    rm -f "$build/.setup-tests-passed"
    cmake --build "$build" > "$build/.setup-build.log" || die "building failed: $build/.setup-build.log"
fi
if [[ ! -f $build/.setup-tests-passed ]]; then
    echo "testing the tracker (ctest)"
    ctest --test-dir "$build" --output-on-failure --timeout 900 > "$build/.setup-ctest.log" ||
        die "tests failed: $build/.setup-ctest.log"
    touch "$build/.setup-tests-passed"
fi
cmake --install "$build" > /dev/null

# install/env.sh: the tracker, Durchblick's C++ programs and zenohd on PATH; the tracker's model
{
    echo "# Written by tools/setup.sh: puts xblick-k4abt's programs on PATH. Use: source install/env.sh"
    echo "export PATH=\"$install/bin:$xblick/bin:$zenoh/bin\${PATH:+:\$PATH}\""
    echo "export K4A_BODY_TRACKING_DNN_MODEL_FILEPATH=\"$k4abt/usr/bin/dnn_model_2_0_op11.onnx\""
} > "$install/env.sh"

echo "xblick-k4abt: set up and tested. To start it beside a running xblick-azure-kinect:"
echo "  source $install/env.sh"
echo "  zenohd --no-multicast-scouting -l tcp/0.0.0.0:7447 &   # once per PC, unless a router runs already"
echo "  xblick-k4abt"
