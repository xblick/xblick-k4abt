#!/bin/bash
# Sets xblick-azure-kinect up from scratch or updates it: the prerequisites, the build, the tests, the installation.
#
#     tools/setup.sh [--cuda | --no-gpu] [--cuda-toolkit DIR] [--body-tracking]
#
# * Prerequisites (pinned versions, each download checked against its sha256) in the node's cache directory of
#   Durchblick, cache_dir("azure-kinect"): $XBLICK_HOME/cache/azure-kinect, else $XDG_CACHE_HOME/xblick/azure-kinect,
#   else ~/.cache/xblick/azure-kinect. Downloads are kept in its downloads/; a file there with the right checksum is
#   not downloaded again, so a file obtained elsewhere can be put there.
# * The build in build-setup/, the tests (ctest), the installation in install/ with install/env.sh, which puts the
#   programs on PATH.
# * Without an option, the CUDA parts are built when nvidia-smi lists a GPU. --cuda: always. --no-gpu: never. They
#   need a CUDA toolkit with nvJPEG and NPP, at /usr/local/cuda or at --cuda-toolkit DIR: 12 or newer (tested: 12.8,
#   another version gives a warning), and 12 with --body-tracking.
# * --body-tracking adds xblick-k4abt: Microsoft's Body Tracking SDK, ONNX Runtime, cuDNN and TensorRT (11 GB of
#   downloads). It needs the CUDA parts.
#
# It never asks and never uses sudo. What needs root it names with the command, then stops (system packages) or goes
# on (the udev rule for the camera). Run again, it does only what changed.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/setup.sh [--cuda | --no-gpu] [--cuda-toolkit DIR] [--body-tracking] [--untested-cuda13]

Installs the prerequisites into the cache directory of xblick (cache/azure-kinect below $XBLICK_HOME, else
$XDG_CACHE_HOME/xblick/azure-kinect, else ~/.cache/xblick/azure-kinect), builds the node in build-setup/, runs its
tests and installs it in install/. Afterwards: source install/env.sh.

  --cuda              build the CUDA parts (nvJPEG decoding, NPP rectification, xblick-points); default when
                      nvidia-smi lists a GPU
  --no-gpu            build without CUDA (the depth engine still needs an NVIDIA GPU with EGL at run time)
  --cuda-toolkit DIR  the CUDA toolkit, 12 or newer, with nvJPEG and NPP (default /usr/local/cuda)
  --body-tracking     also build xblick-k4abt (Body Tracking SDK 1.1.2, ONNX Runtime 1.31.1, cuDNN 9.15,
                      TensorRT 10.14; 11 GB of downloads); needs the CUDA parts
  --untested-cuda13   with --body-tracking and a CUDA 13 toolkit: use the builds of ONNX Runtime, cuDNN and
                      TensorRT for CUDA 13. Body tracking was never tried with them; without this option
                      --body-tracking needs a CUDA 12 toolkit
  -h, --help          this text
EOF
}

die() {
    echo "setup.sh: $*" >&2
    exit 1
}

gpu=auto
toolkit=/usr/local/cuda
body_tracking=0
untested_cuda13=0
while (($#)); do
    case $1 in
        --cuda) [[ $gpu != off ]] || die "--cuda and --no-gpu exclude each other"; gpu=on ;;
        --no-gpu) [[ $gpu != on ]] || die "--cuda and --no-gpu exclude each other"; gpu=off ;;
        --cuda-toolkit) (($# > 1)) || die "--cuda-toolkit needs a directory"; toolkit=$2; shift ;;
        --body-tracking) body_tracking=1 ;;
        --untested-cuda13) untested_cuda13=1 ;;
        -h | --help) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
    shift
done

repo=$(cd "$(dirname "$0")/.." && pwd)

# ------------------------------------------------------------------------------------------------- pinned versions

zenoh_version=1.10.1
xblick_version=0.5.12
json_version=3.12.0
k4a_version=1.4.1
k4abt_version=1.1.2
cuda_header_version=13.4.92
ort_version=1.31.1
cudnn_version=9.15.0.57
cudnn13_version=9.15.0.58
tensorrt_version=10.14.1.48

# file name in downloads/ | URL | sha256
pins=(
    "zenoh-c-$zenoh_version-x86_64-unknown-linux-gnu-standalone.zip|https://github.com/eclipse-zenoh/zenoh-c/releases/download/$zenoh_version/zenoh-c-$zenoh_version-x86_64-unknown-linux-gnu-standalone.zip|9ee0f2d732b0f3042a7e1cd3076042a2bc3ac0415587c40bc3ed7b8b62fbde11"
    "zenoh-cpp-$zenoh_version.tar.gz|https://github.com/eclipse-zenoh/zenoh-cpp/archive/refs/tags/$zenoh_version.tar.gz|64d3c315c4fdfa54b4d96ee27ee2b3d98320b0fb289402fff9d7a99bc513528e"
    "zenoh-$zenoh_version-x86_64-unknown-linux-gnu-standalone.zip|https://github.com/eclipse-zenoh/zenoh/releases/download/$zenoh_version/zenoh-$zenoh_version-x86_64-unknown-linux-gnu-standalone.zip|512ac81438fd995ca2db02f75dd83afa048b6a11630ee10e0cb4dc423c39a5e9"
    "xblick-$xblick_version.tar.gz|https://github.com/xblick/xblick/archive/refs/tags/v$xblick_version.tar.gz|d54a3c5e7e069f9edd7611929a7a1c3ec6acd7d4d4c74185e5969ef5fc949736"
    "nlohmann-json-$json_version.tar.xz|https://github.com/nlohmann/json/releases/download/v$json_version/json.tar.xz|42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa"
    "nvidia_cuda_runtime-$cuda_header_version-py3-none-manylinux2014_x86_64.manylinux_2_17_x86_64.whl|https://files.pythonhosted.org/packages/98/8a/3431271f6344874b8f1ac03f16b3d679c91493f8da63f716160403e6d0a0/nvidia_cuda_runtime-$cuda_header_version-py3-none-manylinux2014_x86_64.manylinux_2_17_x86_64.whl|9641f797da20ce1dd8e779b6e96d08cf9ba564cec8e8225458811ee26423f3a5"
    "libk4a1.4_${k4a_version}_amd64.deb|https://packages.microsoft.com/ubuntu/18.04/prod/pool/main/libk/libk4a1.4/libk4a1.4_${k4a_version}_amd64.deb|c1c63f81641eed1326136a44e5a5fd229e1e6315b2b20b2dddb5523a556c9329"
    "libk4a1.4-dev_${k4a_version}_amd64.deb|https://packages.microsoft.com/ubuntu/18.04/prod/pool/main/libk/libk4a1.4-dev/libk4a1.4-dev_${k4a_version}_amd64.deb|08303094b9ad36ea74c19bc8b8950c97055e73dd2e8bd18e2af5e165a2289cd2"
    "99-k4a.rules|https://raw.githubusercontent.com/microsoft/Azure-Kinect-Sensor-SDK/v1.4.2/scripts/99-k4a.rules|be647ffb970170364c03ab093e336ed4a37d25e6218a25c7f269ac66ca108def"
    "libk4abt1.1_${k4abt_version}_amd64.deb|https://packages.microsoft.com/ubuntu/18.04/prod/pool/main/libk/libk4abt1.1/libk4abt1.1_${k4abt_version}_amd64.deb|0a507ffbe27b49127d53c265b9ef259d0fd993c30765fd612a1ef4054985dc50"
    "libk4abt1.1-dev_${k4abt_version}_amd64.deb|https://packages.microsoft.com/ubuntu/18.04/prod/pool/main/libk/libk4abt1.1-dev/libk4abt1.1-dev_${k4abt_version}_amd64.deb|e04028542d35fcd7d3db2343227afc8e44f3bdc856dbe39eda0f14c31841d55b"
    "onnxruntime-linux-x64-gpu_cuda12-$ort_version.tgz|https://github.com/microsoft/onnxruntime/releases/download/v$ort_version/onnxruntime-linux-x64-gpu_cuda12-$ort_version.tgz|57cd51f677eca19a269cdba31b0c16c15d73a8feea41f170aa8b750447ab1f70"
    "cudnn-linux-x86_64-${cudnn_version}_cuda12-archive.tar.xz|https://developer.download.nvidia.com/compute/cudnn/redist/cudnn/linux-x86_64/cudnn-linux-x86_64-${cudnn_version}_cuda12-archive.tar.xz|183cb46b7b6d5d7c70258ce674cf795611a59a32c220d25e4f772f9f07f77e29"
    "TensorRT-$tensorrt_version.Linux.x86_64-gnu.cuda-12.9.tar.gz|https://developer.download.nvidia.com/compute/machine-learning/tensorrt/10.14.1/tars/TensorRT-$tensorrt_version.Linux.x86_64-gnu.cuda-12.9.tar.gz|0daa7d5929c78edfbe86b474064d0f82d2064c475cc6be747c5101f1ccc37105"
    # the builds for CUDA 13 (--untested-cuda13)
    "onnxruntime-linux-x64-gpu_cuda13-$ort_version.tgz|https://github.com/microsoft/onnxruntime/releases/download/v$ort_version/onnxruntime-linux-x64-gpu_cuda13-$ort_version.tgz|b2146862e691f7e338f5aff9c7a9a5567c2ca57cf01442a1adb3a13bf62d9095"
    "cudnn-linux-x86_64-${cudnn13_version}_cuda13-archive.tar.xz|https://developer.download.nvidia.com/compute/cudnn/redist/cudnn/linux-x86_64/cudnn-linux-x86_64-${cudnn13_version}_cuda13-archive.tar.xz|6ddfeaa00ba534c7917f15666d7fc1e8f05f822a7681eb9adbe4b86beaebe8bc"
    "TensorRT-$tensorrt_version.Linux.x86_64-gnu.cuda-13.0.tar.gz|https://developer.download.nvidia.com/compute/machine-learning/tensorrt/10.14.1/tars/TensorRT-$tensorrt_version.Linux.x86_64-gnu.cuda-13.0.tar.gz|c74af67db57f1a0d7e66bb01ab93f1ecda5facac491ca76e680d832f1e035ce6"
)

# ------------------------------------------------------------------------------------------------- the system

[[ $(uname -m) == x86_64 ]] || die "the SDKs exist for x86-64 only, this is $(uname -m)"
command -v dpkg-query > /dev/null || die "this script needs dpkg (Ubuntu or Debian)"

if [[ $gpu == auto ]]; then
    if command -v nvidia-smi > /dev/null && nvidia-smi -L 2> /dev/null | grep -q '^GPU '; then
        gpu=on
        echo "nvidia-smi lists a GPU: building the CUDA parts (--no-gpu builds without them)"
    else
        gpu=off
        echo "nvidia-smi lists no GPU: building without CUDA (--cuda builds the CUDA parts)"
    fi
fi
((!body_tracking)) || [[ $gpu == on ]] || die "--body-tracking needs the CUDA parts: leave out --no-gpu"
((!untested_cuda13)) || ((body_tracking)) || die "--untested-cuda13 only has a meaning with --body-tracking"

# the Ubuntu packages: the build tools, libjpeg, libpng, EGL; the depth engine needs libGL and libX11 at run time,
# libk4a libudev, the Body Tracking SDK libgomp
packages=(cmake ninja-build g++ curl unzip xz-utils libjpeg-turbo8-dev libpng-dev libegl-dev libgl-dev libgl1
          libx11-6 libudev1)
((!body_tracking)) || packages+=(libgomp1)
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

if [[ $gpu == on ]]; then
    [[ -f $toolkit/version.json && -x $toolkit/bin/nvcc ]] ||
        die "no CUDA toolkit at $toolkit: sudo apt install cuda-toolkit-12-8 (NVIDIA's repository," \
            "https://developer.nvidia.com/cuda-12-8-1-download-archive), or --cuda-toolkit DIR, or --no-gpu"
    toolkit_version=$(sed -n '/"cuda"/,/}/s/.*"version" *: *"\([0-9.]*\)".*/\1/p' "$toolkit/version.json" | head -1)
    toolkit_major=${toolkit_version%%.*}
    [[ $toolkit_major =~ ^[0-9]+$ ]] || die "cannot read the CUDA version from $toolkit/version.json"
    ((toolkit_major >= 12)) ||
        die "the CUDA toolkit at $toolkit is $toolkit_version, the node needs 12 or newer:" \
            "sudo apt install cuda-toolkit-12-8, or --cuda-toolkit DIR, or --no-gpu"
    needed=(include/nvjpeg.h lib64/libnvjpeg.so lib64/libnppig.so)
    if [[ $body_tracking == 1 ]]; then
        # the pinned ONNX Runtime, cuDNN and TensorRT are builds for one CUDA major version and load its libraries
        # by name: the set for CUDA 12 is the tested one, the set for CUDA 13 was never tried with body tracking
        if ((toolkit_major == 13 && untested_cuda13)); then
            echo "setup.sh: WARNING: body tracking with the CUDA 13 builds of ONNX Runtime, cuDNN and TensorRT" \
                "was never tried (--untested-cuda13); going on" >&2
        elif ((toolkit_major == 13)); then
            die "the CUDA toolkit at $toolkit is $toolkit_version, and --body-tracking is tested with CUDA 12 only:" \
                "--untested-cuda13 takes the builds for CUDA 13 (never tried), or --cuda-toolkit DIR of a CUDA 12" \
                "toolkit, or setup without --body-tracking"
        elif ((toolkit_major != 12)); then
            die "the CUDA toolkit at $toolkit is $toolkit_version, but --body-tracking needs CUDA 12 or 13 (ONNX" \
                "Runtime, cuDNN and TensorRT are pinned as builds for these): --cuda-toolkit DIR"
        fi
        needed+=("lib64/libcublas.so.$toolkit_major")
    fi
    for file in "${needed[@]}"; do
        [[ -e $toolkit/$file ]] || die "the CUDA toolkit at $toolkit has no $file (nvJPEG and NPP are needed)"
    done
    [[ $toolkit_version == 12.8* ]] ||
        echo "setup.sh: WARNING: CUDA $toolkit_version is not tested with this node (tested: 12.8); going on" >&2
fi

# ------------------------------------------------------------------------------------------------- the cache

if [[ -n ${XBLICK_HOME:-} ]]; then
    [[ $XBLICK_HOME == /* ]] || die "XBLICK_HOME must be an absolute path: $XBLICK_HOME"
    cache=$XBLICK_HOME/cache/azure-kinect
else
    xdg=${XDG_CACHE_HOME:-}
    [[ $xdg != "~"* ]] || xdg=$HOME${xdg#\~}
    if [[ $xdg == /* ]]; then
        cache=$xdg/xblick/azure-kinect
    else
        cache=$HOME/.cache/xblick/azure-kinect
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
notice_cuda_header="NVIDIA's CUDA runtime package $cuda_header_version (for its header cuda.h) is under the NVIDIA CUDA\
 Toolkit End User License Agreement: https://docs.nvidia.com/cuda/eula/index.html"
notice_cudnn="NVIDIA cuDNN $cudnn_version is under the NVIDIA Software License Agreement for cuDNN:\
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

# The core's C++ package. With CUDA it includes the point cloud node xblick-points and takes cuda.h from the toolkit,
# without it takes cuda.h from NVIDIA's CUDA runtime package on PyPI (as the core's CI does).
if [[ $gpu == on ]]; then
    xblick=$cache/xblick-$xblick_version-cuda
else
    xblick=$cache/xblick-$xblick_version
fi
if ! finished "$xblick"; then
    fetch "xblick-$xblick_version.tar.gz"
    tar -xzf "$downloads/xblick-$xblick_version.tar.gz" -C "$cache/src"
    core_options=(-DXBLICK_POINTS=OFF)
    if [[ $gpu == on ]]; then
        core_options=(-DXBLICK_POINTS=ON -DCUDAToolkit_ROOT="$toolkit" -DCMAKE_CUDA_COMPILER="$toolkit/bin/nvcc"
                      -DXBLICK_CUDA_INCLUDE_DIR="$toolkit/include")
    else
        header=$cache/cuda-header-$cuda_header_version
        if ! finished "$header"; then
            fetch "nvidia_cuda_runtime-$cuda_header_version-py3-none-manylinux2014_x86_64.manylinux_2_17_x86_64.whl" \
                "$notice_cuda_header"
            mkdir -p "$header"
            unzip -qo "$downloads/nvidia_cuda_runtime-$cuda_header_version-py3-none-manylinux2014_x86_64.manylinux_2_17_x86_64.whl" \
                'nvidia/cu13/include/*' -d "$header"
            mark_finished "$header"
        fi
        core_options+=(-DXBLICK_CUDA_INCLUDE_DIR="$header/nvidia/cu13/include")
    fi
    echo "building Durchblick $xblick_version (C++) into $xblick"
    cmake -S "$cache/src/xblick-$xblick_version" -B "$cache/build/${xblick##*/}" -G Ninja \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$xblick" -DCMAKE_PREFIX_PATH="$zenoh" \
        -DBUILD_TESTING=OFF "${core_options[@]}" > /dev/null
    cmake --build "$cache/build/${xblick##*/}" > /dev/null
    cmake --install "$cache/build/${xblick##*/}" > /dev/null
    mark_finished "$xblick"
fi

# ------------------------------------------------------------------------------------------------- Sensor SDK

# Microsoft's packages unpacked as they are (usr/...): libk4a finds the depth engine in usr/lib/x86_64-linux-gnu/
# libk4a1.4 through its own RUNPATH ($ORIGIN/libk4a1.4).
k4a=$cache/k4a-$k4a_version
if ! finished "$k4a"; then
    fetch "libk4a1.4_${k4a_version}_amd64.deb" "$notice_k4a"
    fetch "libk4a1.4-dev_${k4a_version}_amd64.deb"
    fetch 99-k4a.rules
    echo "unpacking the Azure Kinect Sensor SDK $k4a_version"
    mkdir -p "$k4a"
    dpkg-deb -x "$downloads/libk4a1.4_${k4a_version}_amd64.deb" "$k4a"
    dpkg-deb -x "$downloads/libk4a1.4-dev_${k4a_version}_amd64.deb" "$k4a"
    cp "$downloads/99-k4a.rules" "$k4a/99-k4a.rules"
    mark_finished "$k4a"
fi

# the udev rule that lets a user other than root open the camera (scripts/99-k4a.rules of the Sensor SDK)
if ! grep -qs '045e.*097c' /etc/udev/rules.d/*.rules /lib/udev/rules.d/*.rules; then
    echo "notice: no udev rule for the Azure Kinect, so only root can open a camera. Install it with:"
    echo "  sudo cp $k4a/99-k4a.rules /etc/udev/rules.d/ && sudo udevadm control --reload-rules && sudo udevadm trigger"
fi

# ------------------------------------------------------------------------------------------------- body tracking

node_options=(-DXBLICK_K4A_BODY_TRACKING=OFF)
if ((body_tracking)); then
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
    # the set of builds for the toolkit's CUDA major version (12: tested; 13: --untested-cuda13)
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
        echo "unpacking ONNX Runtime $ort_version"
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
        echo "unpacking TensorRT $tensorrt_version (its libraries)"
        mkdir -p "$tensorrt_root"
        tar -xzf "$downloads/$tensorrt_name.tar.gz" -C "$tensorrt_root" \
            --exclude='*.a' --exclude='*_win_*' "TensorRT-$tensorrt_version/lib" "TensorRT-$tensorrt_version/doc"
        mark_finished "$tensorrt"
    fi
    node_options=(-DXBLICK_K4A_BODY_TRACKING=ON -Dk4abt_DIR="$k4abt/usr/lib/cmake/k4abt"
                  -DXBLICK_K4A_ONNXRUNTIME_DIR="$ort" -DXBLICK_K4A_CUDNN_DIR="$cudnn"
                  -DXBLICK_K4A_TENSORRT_DIR="$tensorrt"
                  -DK4ABT_TEST_MODEL="$k4abt/usr/bin/dnn_model_2_0_op11.onnx" -DK4ABT_TEST_PROCESSING=tensorrt)
fi

# ------------------------------------------------------------------------------------------------- the node

build=$repo/build-setup
install=$repo/install
if [[ $gpu == on ]]; then
    node_options+=(-DXBLICK_K4A_CUDA=ON -DCUDAToolkit_ROOT="$toolkit")
else
    node_options+=(-DXBLICK_K4A_CUDA=OFF)
fi
# every package by its directory: a directory cached by an earlier call with other options would win over the
# prefix path
configure=(cmake -S "$repo" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$install"
           -DCMAKE_PREFIX_PATH="$xblick;$zenoh;$json;$k4a/usr" -Dxblick_DIR="$xblick/lib/cmake/xblick"
           -Dzenohc_DIR="$zenoh/lib/cmake/zenohc" -Dzenohcxx_DIR="$zenoh/lib/cmake/zenohcxx"
           -Dnlohmann_json_DIR="$json/share/cmake/nlohmann_json" -Dk4a_DIR="$k4a/usr/lib/x86_64-linux-gnu/cmake/k4a"
           -Dk4arecord_DIR="$k4a/usr/lib/x86_64-linux-gnu/cmake/k4arecord" "${node_options[@]}")
mkdir -p "$build"
if [[ ! -f $build/CMakeCache.txt || "$(cat "$build/.setup-configure" 2> /dev/null)" != "${configure[*]}" ]]; then
    echo "configuring the node in $build"
    rm -f "$build/.setup-tests-passed"
    "${configure[@]}" > "$build/.setup-configure.log" || die "configuring failed: $build/.setup-configure.log"
    echo "${configure[*]}" > "$build/.setup-configure"
fi
if [[ $(ninja -C "$build" -n 2>&1 | tail -1) != "ninja: no work to do." ]]; then
    echo "building the node"
    rm -f "$build/.setup-tests-passed"
    cmake --build "$build" > "$build/.setup-build.log" || die "building failed: $build/.setup-build.log"
fi
if [[ ! -f $build/.setup-tests-passed ]]; then
    echo "testing the node (ctest)"
    ctest --test-dir "$build" --output-on-failure --timeout 900 > "$build/.setup-ctest.log" ||
        die "tests failed: $build/.setup-ctest.log"
    touch "$build/.setup-tests-passed"
fi
cmake --install "$build" > /dev/null

# install/env.sh: the programs of the node, of Durchblick's C++ package and zenohd on PATH; the tracker's model
{
    echo "# Written by tools/setup.sh: puts xblick-azure-kinect's programs on PATH. Use: source install/env.sh"
    echo "export PATH=\"$install/bin:$xblick/bin:$zenoh/bin\${PATH:+:\$PATH}\""
    if ((body_tracking)); then
        echo "export K4A_BODY_TRACKING_DNN_MODEL_FILEPATH=\"$k4abt/usr/bin/dnn_model_2_0_op11.onnx\""
    fi
} > "$install/env.sh"

echo "xblick-azure-kinect: set up and tested. To start it:"
echo "  source $install/env.sh"
echo "  zenohd --no-multicast-scouting -l tcp/0.0.0.0:7447 &   # once per PC, unless a router runs already"
echo "  xblick-azure-kinect"
if ((body_tracking)); then
    echo "  xblick-k4abt"
fi
