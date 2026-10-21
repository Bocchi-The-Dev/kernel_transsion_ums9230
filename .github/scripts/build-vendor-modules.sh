#!/usr/bin/env bash
#
# Build the out-of-tree vendor modules under kernel_modules/.
#
# These modules normally live in the Unisoc Android build and are compiled by
# its module build system, which supplies BSP_KERNEL_PATH, BSP_MODULES_OUT and
# a board-specific MALI_PLATFORM_NAME.  Reproduce just enough of that
# environment to build everything that can be built against bg6_defconfig.
#
# Vendor modules are not all valid for one board: there are alternate GPU
# drivers, touch controllers for other panels, cameras for other sensors, and
# modules that expect Android HAL symbols that are not in this tree.  A
# failure is therefore expected and only reported.  It must not stop the
# remaining modules from being built, nor fail the CI job.

set -u

top=$(pwd)
kernel_out=${KERNEL_OUT:-$top/out}
modules_out=${BSP_MODULES_OUT:-$top/vendor_modules}
artifact=${ARTIFACT_DIR:-$top/vendor-modules-artifact}
jobs=${JOBS:-$(nproc)}
module_timeout=${MODULE_TIMEOUT:-1800}

export BSP_KERNEL_PATH="$kernel_out"
export BSP_MODULES_OUT="$modules_out"
export ARCH=arm64
export CROSS_COMPILE=${CROSS_COMPILE:-aarch64-linux-gnu-}
export LLVM=${LLVM:-1}
export LLVM_IAS=${LLVM_IAS:-1}
export MAKEFLAGS="-j$jobs"

# The vendor tree compiles with -Werror globally.  Several out-of-tree
# drivers have harmless warnings that older vendor toolchains did not enable.
# Downgrade only warnings that cannot change the generated API: an undeclared
# function or a missing type is still an error, so modules that really do not
# match this kernel keep failing instead of silently producing an unloadable
# .ko.
export KCFLAGS="${KCFLAGS:-} -Wno-error=misleading-indentation \
-Wno-error=format -Wno-error=format-extra-args \
-Wno-error=unused-variable -Wno-error=unused-function \
-Wno-error=unused-but-set-variable -Wno-error=address-of-packed-member \
-Wno-error=maybe-uninitialized -Wno-error=uninitialized"

mkdir -p "$modules_out" "$artifact/logs" "$artifact/ko"

# These are either not part of this device's vendor image or are built by a
# dedicated step with extra platform flags:
#   gpu/gondul, gpu/midgard, gpu/rogue : alternate GPU drivers
#   gpu/natt                           : built by the CI Mali step
#   sample                             : test module
exclude='(^|/)(sample|gpu/(gondul|midgard|natt|rogue))(/|$)'

# Build the modules that other modules import first.  A second pass below
# retries anything that failed, which covers the remaining dependency order
# (camera/core needs the camera power domain, cpp needs ion/dmabuf, ...).
mapfile -t dirs < <(
  find kernel_modules -name Makefile -print0 |
    xargs -0 grep -l 'KO_MODULE_NAME' |
    xargs -n1 dirname |
    sort -u |
    grep -Ev "$exclude" |
    while IFS= read -r dir; do
      case "$dir" in
        */camera/power|*/camera/flash/flash_drv|*/ion/sprd_ion|*/video/*-power)
          prio=0 ;;
        */audio_driver/sprd_audio/*)
          prio=1 ;;
        */audio_driver/sprd/*)
          prio=2 ;;
        */camera/*)
          prio=3 ;;
        *)
          prio=4 ;;
      esac
      printf '%s %s\n' "$prio" "$dir"
    done |
    sort -k1,1n -k2,2 |
    cut -d' ' -f2-
)

# Optional debugging aid: MODULE_FILTER is an extended regex matched against
# the module directory.  CI leaves it empty and builds everything.
if [ -n "${MODULE_FILTER:-}" ]; then
  mapfile -t dirs < <(printf '%s\n' "${dirs[@]}" | grep -E "$MODULE_FILTER")
fi

if [ ${#dirs[@]} -eq 0 ]; then
  echo "No vendor module Makefiles found" >&2
  exit 0
fi

echo "Building ${#dirs[@]} vendor module directories"

declare -A module_name
declare -A built
declare -A result

for dir in "${dirs[@]}"; do
  name=$(sed -n \
    's/^[[:space:]]*\(export[[:space:]]\+\)\?KO_MODULE_NAME[[:space:]]*:=[[:space:]]*//p' \
    "$dir/Makefile" | head -1)
  [ -n "$name" ] || name=$(basename "$dir")
  module_name[$dir]=$name
done

for pass in 1 2; do
  pending=0
  for dir in "${dirs[@]}"; do
    [ "${built[$dir]:-0}" = 1 ] && continue
    pending=$((pending + 1))

    name=${module_name[$dir]}
    log="$artifact/logs/$(printf '%s' "$dir" | tr '/' '_').log"

    echo
    echo "== pass $pass: $dir ($name)"
    if timeout "$module_timeout" make -C "$dir" modules >"$log" 2>&1; then
      # A few modules have an empty obj-m under this defconfig and correctly
      # produce no .ko.  That is a skip, not a failure.
      if find "$modules_out/$name" -name '*.ko' -print -quit 2>/dev/null |
         grep -q .; then
        built[$dir]=1
        result[$dir]="built"
        echo "   built $name.ko"
        continue
      fi
      result[$dir]="no .ko (skipped by its Kbuild)"
      echo "   no .ko produced"
    else
      result[$dir]="failed"
      echo "   failed"
      tail -n 5 "$log" | sed 's/^/     /'
    fi
  done

  [ "$pending" -eq 0 ] && break
done

# Collect every module that was built: in-tree modules (including vgem and
# the modules the external ones import) plus the external vendor modules.
copy_kos() {
  src=$1
  [ -d "$src" ] || return 0
  (cd "$src" && find . -name '*.ko' -print0 |
    while IFS= read -r -d '' ko; do
      cp --parents "$ko" "$artifact/ko/" 2>/dev/null || true
    done)
}

copy_kos "$kernel_out"
copy_kos "$modules_out"

# Debug sections are large and are not needed for the artifact.  Use the
# same toolchain that built the modules when it is available.
strip_tool=${CLANG_DIR:+$CLANG_DIR/bin/llvm-strip}
if [ -x "$strip_tool" ]; then
  find "$artifact/ko" -name '*.ko' -exec "$strip_tool" --strip-debug {} + \
    >/dev/null 2>&1 || true
elif command -v llvm-strip >/dev/null 2>&1; then
  find "$artifact/ko" -name '*.ko' -exec llvm-strip --strip-debug {} + \
    >/dev/null 2>&1 || true
fi

if [ -f /tmp/in-tree-modules.log ]; then
  cp /tmp/in-tree-modules.log "$artifact/logs/in-tree-modules.log"
fi

ko_count=$(find "$artifact/ko" -name '*.ko' | wc -l)
built_count=0
failed_count=0
for dir in "${dirs[@]}"; do
  case "${result[$dir]:-}" in
    built) built_count=$((built_count + 1)) ;;
    failed) failed_count=$((failed_count + 1)) ;;
  esac
done

{
  echo "# Vendor module build report"
  echo
  echo "- out-of-tree module directories attempted: ${#dirs[@]}"
  echo "- out-of-tree modules built: $built_count"
  echo "- out-of-tree modules failed: $failed_count"
  echo "- .ko files collected (in-tree + vendor): $ko_count"
  echo
  echo "## Out-of-tree results"
  echo
  echo "| Module | Result |"
  echo "|---|---|"
  for dir in "${dirs[@]}"; do
    printf '| `%s` (%s) | %s |\n' \
      "${module_name[$dir]}" "$dir" "${result[$dir]:-not run}"
  done
} > "$artifact/BUILD-REPORT.md"

echo
echo "Collected $ko_count .ko files; report in $artifact/BUILD-REPORT.md"
exit 0
