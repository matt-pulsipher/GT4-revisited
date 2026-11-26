#!/usr/bin/env bash
# Builds GT4 Revisited for Linux, in Docker (from macOS or Linux) or on this Linux machine (--native),
# and optionally runs the unit tests. docs/BUILDING.md explains the options.
#
#   tools/linux/build.sh [--arch x86_64|arm64] [--compiler gcc|clang] [--base ubuntu:24.04]
#                        [--type RelWithDebInfo] [--jobs 4] [--build-dir DIR] [--native]
#                        [--target T]... [--keep-going] [--test [FILTER]] [--exec COMMAND]
#                        [-- extra cmake arguments]
#
# The tests (and --exec, a shell command run in the build folder after the build) read the discs named in
# GT4R_DISC (";"-separated paths); in Docker each one is mounted read-only and GT4R_DISC names the mounted
# copies (GT4R_SIM_STATES, a folder of the original's saved states, likewise). Both run headless (SDL's dummy video and audio drivers).
set -euo pipefail

src=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
arch=""
compiler=gcc
base=ubuntu:24.04
type=RelWithDebInfo
jobs=4
build_dir=""
native=0
keep_going=0
run_tests=0
test_filter=""
exec_cmd=""
targets=()
cmake_extra=()

die() { echo "build.sh: $*" >&2; exit 2; }
while [[ $# -gt 0 ]]; do
  case $1 in
    --arch) arch=$2; shift 2 ;;
    --compiler) compiler=$2; shift 2 ;;
    --base) base=$2; shift 2 ;;
    --type) type=$2; shift 2 ;;
    --jobs|-j) jobs=$2; shift 2 ;;
    --build-dir) build_dir=$2; shift 2 ;;
    --native) native=1; shift ;;
    --keep-going) keep_going=1; shift ;;
    --target) targets+=("$2"); shift 2 ;;
    --test)
      run_tests=1; shift
      if [[ $# -gt 0 && $1 != --* ]]; then test_filter=$1; shift; fi ;;
    --exec) exec_cmd=$2; shift 2 ;;
    --) shift; cmake_extra=("$@"); break ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) die "unknown option $1 (see --help)" ;;
  esac
done

case $compiler in
  gcc) cc=gcc; cxx=g++ ;;
  clang) cc=clang; cxx=clang++ ;;
  *) die "--compiler is gcc or clang" ;;
esac

if [[ $native == 1 ]]; then
  [[ $(uname -s) == Linux ]] || die "--native builds on Linux only; elsewhere leave it out to build in Docker"
  [[ -n $build_dir ]] || build_dir=$src/build-linux-$(uname -m)-$compiler
  [[ -f $src/engine/gt3r/CMakeLists.txt ]] || die "engine/gt3r is missing: git submodule update --init"
  generator=()
  command -v ninja >/dev/null && generator=(-G Ninja)
  CC=$cc CXX=$cxx cmake -S "$src" -B "$build_dir" ${generator[@]+"${generator[@]}"} -DCMAKE_BUILD_TYPE="$type" ${cmake_extra[@]+"${cmake_extra[@]}"}
  keep=()
  if [[ $keep_going == 1 ]]; then
    if grep -q '^CMAKE_GENERATOR:INTERNAL=Ninja' "$build_dir/CMakeCache.txt"; then keep=(-- -k 0); else keep=(-- -k); fi
  fi
  if [[ ${#targets[@]} -gt 0 ]]; then
    cmake --build "$build_dir" -j "$jobs" --target ${targets[@]+"${targets[@]}"} ${keep[@]+"${keep[@]}"}
  else
    cmake --build "$build_dir" -j "$jobs" ${keep[@]+"${keep[@]}"}
  fi
  export SDL_VIDEO_DRIVER=${SDL_VIDEO_DRIVER:-dummy} SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy}
  status=0
  if [[ $run_tests == 1 ]]; then
    log=$build_dir/tests.log
    echo "build.sh: running the tests (log: $log)"
    set +e
    "$build_dir/gt4r_tests" ${test_filter:+"$test_filter"} > "$log" 2>&1
    status=$?
    set -e
    grep -E '^ *FAIL|^[0-9]+ run, ' "$log" || true
  fi
  if [[ -n $exec_cmd ]]; then
    (cd "$build_dir" && bash -c "$exec_cmd") || status=$?
  fi
  exit $status
fi

# Docker: an image per base and architecture, built from tools/linux/Dockerfile.
command -v docker >/dev/null || die "docker is not installed (or use --native on Linux)"
if [[ -z $arch ]]; then
  case $(uname -m) in x86_64|amd64) arch=x86_64 ;; arm64|aarch64) arch=arm64 ;; *) die "set --arch" ;; esac
fi
case $arch in
  x86_64|amd64) arch=x86_64; platform=linux/amd64 ;;
  arm64|aarch64) arch=arm64; platform=linux/arm64 ;;
  *) die "--arch is x86_64 or arm64" ;;
esac
image=gt4r-linux-build:${base//[:\/]/-}-$arch
docker build --platform "$platform" --build-arg BASE="$base" -t "$image" -q "$src/tools/linux" >/dev/null

[[ -n $build_dir ]] || build_dir=build-linux-$arch-$compiler-${base//[:\/]/-}
case $build_dir in /*) die "--build-dir is a folder name inside the source tree when building in Docker" ;; esac
mkdir -p "$src/$build_dir"

mounts=(--mount "type=bind,source=$src,target=/src,readonly"
        --mount "type=bind,source=$src/$build_dir,target=/src/$build_dir")
args=(--native)
[[ $keep_going == 1 ]] && args+=(--keep-going)
args+=(--compiler "$compiler" --type "$type" --jobs "$jobs" --build-dir "/src/$build_dir")
for t in ${targets[@]+"${targets[@]}"}; do args+=(--target "$t"); done
env=()
[[ $run_tests == 1 ]] && args+=(--test ${test_filter:+"$test_filter"})
[[ -n $exec_cmd ]] && args+=(--exec "$exec_cmd")
if [[ $run_tests == 1 || -n $exec_cmd ]]; then
  discs=""
  if [[ -n ${GT4R_DISC:-} ]]; then
    IFS=';' read -r -a paths <<< "$GT4R_DISC"
    for p in "${paths[@]}"; do
      [[ -f $p ]] || die "GT4R_DISC names $p, which is not a file"
      name=$(basename "$p")
      [[ $name != *,* ]] || die "a disc's file name cannot contain a comma here: $name"
      mounts+=(--mount "type=bind,source=$p,target=/discs/$name,readonly")
      discs+="${discs:+;}/discs/$name"
    done
  fi
  env+=(-e "GT4R_DISC=$discs")
  # The original's saved states some tests read (GT4R_SIM_STATES, a folder), mounted read-only as well.
  if [[ -n ${GT4R_SIM_STATES:-} ]]; then
    [[ -d $GT4R_SIM_STATES ]] || die "GT4R_SIM_STATES names $GT4R_SIM_STATES, which is not a folder"
    mounts+=(--mount "type=bind,source=$GT4R_SIM_STATES,target=/states,readonly")
    env+=(-e GT4R_SIM_STATES=/states)
  fi
fi
args+=(-- ${cmake_extra[@]+"${cmake_extra[@]}"})

docker run --rm --platform "$platform" --user "$(id -u):$(id -g)" \
  -e HOME=/tmp/home -e GT4R_DATA_DIR=/tmp/gt4r-data ${env[@]+"${env[@]}"} ${mounts[@]+"${mounts[@]}"} \
  "$image" bash -c 'mkdir -p "$HOME" "$GT4R_DATA_DIR" && exec /src/tools/linux/build.sh "$@"' build.sh "${args[@]}"
