#!/usr/bin/env bash
# Build Multi-Compressor on macOS. No tests are run.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./build-macos.sh [vst3|au|all] [options]

  --arch arm64|x86_64|universal   Default: current shell architecture
  --deployment-target VERSION   Default: 11.0 (arm64/universal), 10.15 (Intel)
  --parallel NUMBER             Build jobs (default: 4)
  --install                     Copy bundles into your user plug-in folders
  --help                        Show this help

Examples:
  ./build-macos.sh vst3 --arch arm64 --install
  ./build-macos.sh au --arch arm64 --install
  ./build-macos.sh all --arch universal
EOF
}

format=${1:-all}
case "$format" in
    -h|--help) usage; exit 0 ;;
    vst3|au|all) ;;
    *) usage >&2; exit 2 ;;
esac
if [[ $# -gt 0 ]]; then shift; fi
architecture=""
deployment=""
parallel=4
install_requested=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch|--deployment-target|--parallel)
            if [[ $# -lt 2 ]]; then
                printf 'Missing value for %s\n' "$1" >&2
                exit 2
            fi
            case "$1" in
                --arch) architecture=$2 ;;
                --deployment-target) deployment=$2 ;;
                --parallel) parallel=$2 ;;
            esac
            shift 2 ;;
        --install) install_requested=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ $(uname -s) != Darwin ]]; then
    printf 'Run this script on macOS with Xcode and CMake 3.22 or newer.\n' >&2
    exit 1
fi
architecture=${architecture:-$(uname -m)}
case "$architecture" in
    arm64) cmake_arch=arm64; deployment=${deployment:-11.0} ;;
    x86_64) cmake_arch=x86_64; deployment=${deployment:-10.15} ;;
    universal) cmake_arch='arm64;x86_64'; deployment=${deployment:-11.0} ;;
    *) printf 'Unsupported architecture: %s\n' "$architecture" >&2; exit 2 ;;
esac
if [[ ! $parallel =~ ^[1-9][0-9]*$ || ! $deployment =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]; then
    printf 'Use a positive job count and a macOS version such as 11.0.\n' >&2
    exit 2
fi
for tool in cmake xcodebuild; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Required tool missing: %s. See README.md for setup.\n' "$tool" >&2
        exit 1
    fi
done
xcodebuild -version

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
build_dir="$repo_root/build-yinyang-mac-$architecture"
targets=()
if [[ $format == vst3 || $format == all ]]; then targets+=(YinyangCompressor_VST3); fi
if [[ $format == au || $format == all ]]; then targets+=(YinyangCompressor_AU); fi

cmake -S "$repo_root/projects/YinyangCompressor" -B "$build_dir" -G Xcode \
    "-DCMAKE_OSX_ARCHITECTURES=$cmake_arch" \
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$deployment" \
    -DYINYANG_COMPRESSOR_BUILD_TESTS=OFF
cmake --build "$build_dir" --config Release --target "${targets[@]}" --parallel "$parallel"

artifacts="$build_dir/YinyangCompressor_artefacts/Release"
if [[ $format == vst3 || $format == all ]]; then
    bundle="$artifacts/VST3/Yinyang Compressor.vst3"
    [[ -d "$bundle" ]] || { printf 'Missing VST3 bundle: %s\n' "$bundle" >&2; exit 1; }
    printf 'Built: %s\n' "$bundle"
    if [[ $install_requested == true ]]; then
        destination="$HOME/Library/Audio/Plug-Ins/VST3"
        mkdir -p "$destination"
        ditto "$bundle" "$destination/Yinyang Compressor.vst3"
        printf 'Installed: %s/Yinyang Compressor.vst3\n' "$destination"
    fi
fi
if [[ $format == au || $format == all ]]; then
    bundle="$artifacts/AU/Yinyang Compressor.component"
    [[ -d "$bundle" ]] || { printf 'Missing AU bundle: %s\n' "$bundle" >&2; exit 1; }
    printf 'Built: %s\n' "$bundle"
    if [[ $install_requested == true ]]; then
        destination="$HOME/Library/Audio/Plug-Ins/Components"
        mkdir -p "$destination"
        ditto "$bundle" "$destination/Yinyang Compressor.component"
        printf 'Installed: %s/Yinyang Compressor.component\n' "$destination"
    fi
fi
