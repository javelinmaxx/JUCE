#!/usr/bin/env bash
# Build every JUCE plugin project as VST3 and AU with macOS's Bash 3.2.
set -euo pipefail

usage() {
    cat <<'HELP'
Usage: ./build-macos-all.sh [options]

Build all plugin projects under projects/ in Release as both VST3 and AU.

  --arch arm64|x86_64|universal   Default: current shell architecture
  --deployment-target VERSION   Default: 11.0 (arm64/universal), 10.15 (Intel)
  --parallel NUMBER             Build jobs per project (default: 4)
  --build-root PATH             Default: <repo>/build/macos-all/<architecture>
  --install                     Copy bundles into your user plug-in folders
  --help                        Show this help

Requires macOS, full Xcode, and CMake 3.22 or newer. Tests are not built or run.
Projects build sequentially; --parallel controls jobs within each project.

Examples:
  ./build-macos-all.sh
  ./build-macos-all.sh --arch universal --parallel 4
  ./build-macos-all.sh --arch arm64 --install
HELP
}

architecture=""
deployment=""
build_root=""
parallel=4
install_requested=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch|--deployment-target|--parallel|--build-root)
            if [[ $# -lt 2 || -z $2 || $2 == --* ]]; then
                printf 'Missing value for %s\n' "$1" >&2
                exit 2
            fi
            case "$1" in
                --arch) architecture=$2 ;;
                --deployment-target) deployment=$2 ;;
                --parallel) parallel=$2 ;;
                --build-root) build_root=$2 ;;
            esac
            shift 2 ;;
        --install) install_requested=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
done

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
if [[ $(uname -s) != Darwin ]]; then
    printf 'Run this script on macOS with Xcode and CMake 3.22 or newer.\n' >&2
    exit 1
fi
for tool in cmake xcodebuild; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'Required tool missing: %s. See README.md for setup.\n' "$tool" >&2
        exit 1
    fi
done
xcodebuild -version

repo_root=$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_root=${build_root:-"$repo_root/build/macos-all/$architecture"}
mkdir -p "$build_root"
build_root=$(cd -- "$build_root" && pwd)
shopt -s nullglob
built_bundles=()
project_count=0

for cmake_file in "$repo_root"/projects/*/CMakeLists.txt; do
    # Each independent project declares literal JUCE target names. Handle both
    # one-line declarations and names on the line after juce_add_plugin(.
    target_names=$(awk '
        /^[[:space:]]*juce_add_plugin[[:space:]]*\(/ {
            sub(/^[[:space:]]*juce_add_plugin[[:space:]]*\([[:space:]]*/, "")
            waiting = 1
        }
        waiting {
            sub(/^[[:space:]]+/, "")
            if ($0 == "" || $0 ~ /^#/) next
            if (match($0, /^[A-Za-z_][A-Za-z0-9_]*/)) {
                print substr($0, RSTART, RLENGTH)
            } else {
                print "Cannot read a literal JUCE plugin target in " FILENAME > "/dev/stderr"
                exit 1
            }
            waiting = 0
        }
        END { if (waiting) exit 1 }
    ' "$cmake_file")
    [[ -n $target_names ]] || continue
    project_dir=$(dirname "$cmake_file")
    project_name=$(basename "$project_dir")
    build_dir="$build_root/$project_name"
    plugin_targets=()
    build_targets=()
    while IFS= read -r target; do
        plugin_targets+=("$target")
        build_targets+=("${target}_VST3" "${target}_AU")
    done <<< "$target_names"
    test_options=()
    while IFS= read -r option; do
        [[ -n $option ]] || continue
        test_options+=("-D$option=OFF")
    done < <(sed -nE 's/^[[:space:]]*option[[:space:]]*\([[:space:]]*([A-Za-z_][A-Za-z0-9_]*BUILD_TESTS)[[:space:]].*/\1/p' "$cmake_file")

    printf '\nBuilding %s (%s; VST3 + AU)\n' "$project_name" "$architecture"
    cmake -S "$project_dir" -B "$build_dir" -G Xcode \
        "-DCMAKE_OSX_ARCHITECTURES=$cmake_arch" \
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=$deployment" \
        ${test_options[@]+"${test_options[@]}"}
    cmake --build "$build_dir" --config Release --target "${build_targets[@]}" --parallel "$parallel"

    for target in "${plugin_targets[@]}"; do
        artifacts="$build_dir/${target}_artefacts/Release"
        vst_bundles=("$artifacts/VST3/"*.vst3)
        au_bundles=("$artifacts/AU/"*.component)
        if [[ ${#vst_bundles[@]} -eq 0 || ${#au_bundles[@]} -eq 0 ]]; then
            printf 'Missing VST3 or AU bundle for %s in %s\n' "$target" "$artifacts" >&2
            exit 1
        fi
        for bundle in "${vst_bundles[@]}" "${au_bundles[@]}"; do
            [[ -d $bundle ]] || { printf 'Missing bundle directory: %s\n' "$bundle" >&2; exit 1; }
            built_bundles+=("$bundle")
        done
    done
    project_count=$((project_count+1))
done

if [[ $project_count -eq 0 ]]; then
    printf 'No JUCE plugin projects found under %s/projects.\n' "$repo_root" >&2
    exit 1
fi
printf '\nBuilt %s projects (%s bundles):\n' "$project_count" "${#built_bundles[@]}"
printf '  %s\n' "${built_bundles[@]}"
if [[ $install_requested == true ]]; then
    for bundle in "${built_bundles[@]}"; do
        case "$bundle" in
            *.vst3) destination="$HOME/Library/Audio/Plug-Ins/VST3" ;;
            *.component) destination="$HOME/Library/Audio/Plug-Ins/Components" ;;
        esac
        mkdir -p "$destination"
        ditto "$bundle" "$destination/$(basename "$bundle")"
        printf 'Installed: %s/%s\n' "$destination" "$(basename "$bundle")"
    done
fi
