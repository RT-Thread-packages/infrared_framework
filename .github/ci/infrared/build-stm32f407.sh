#!/usr/bin/env sh
# SPDX-License-Identifier: Apache-2.0
# RT-Thread 4.1.1 and master STM32F407 SCons compile/link matrix.
set -eu

fail()
{
    printf 'INFRARED_STM32_BUILD_FAIL: %s\n' "$*" >&2
    exit 1
}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
package_root=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
matrix="$package_root/tools/ci/check_matrix.py"

case "${1:-}" in
    --list-profiles)
        [ "$#" -eq 1 ] || fail 'Usage: build-stm32f407.sh --list-profiles'
        exec python3 "$matrix" --list-profiles ;;
    '')
        profile=${INFRARED_CI_PROFILE:-framework-core} ;;
    *)
        [ "$#" -eq 1 ] || fail 'Usage: build-stm32f407.sh [PROFILE]'
        profile=$1 ;;
esac

case "$profile" in
    legacy-minimal|legacy-nec-rx|legacy-rx-tx|framework-core|framework-pin-nec|\
    framework-capture|framework-pwm|framework-full|framework-sparse|framework-pulse-only) ;;
    *) fail "unknown CI profile: $profile" ;;
esac

for tool in python3 git scons arm-none-eabi-gcc arm-none-eabi-readelf arm-none-eabi-size; do
    command -v "$tool" >/dev/null 2>&1 || fail "missing build tool: $tool"
done

ci_dir="${GITHUB_WORKSPACE:-$package_root}/_ci"
rtthread_dir="$ci_dir/rt-thread"
bsp_rel=${RTTHREAD_BSP:-bsp/stm32/stm32f407-atk-explorer}
bsp_dir="$rtthread_dir/$bsp_rel"
# v4.1.1 covers legacy HWTIMER; master covers backends not requiring it.
rtthread_ref=${RTTHREAD_REF:-v4.1.1}
case "$rtthread_ref" in
    v4.1.1|master) ;;
    *) fail "unsupported RT-Thread ref: $rtthread_ref" ;;
esac
if [ "$rtthread_ref" = master ]; then
    case "$profile" in
        legacy-minimal|framework-core|framework-capture|framework-pulse-only) ;;
        *) fail "profile $profile requires legacy HWTIMER APIs unavailable on master" ;;
    esac
fi
profile_header="$ci_dir/infrared-profile-$profile.h"
revisions="$ci_dir/infrared-revisions-$profile.txt"
mkdir -p "$ci_dir/logs"

# Parse the actual package Kconfig before injecting the effective macros.
python3 "$matrix" --profile "$profile" --emit-header "$profile_header" || \
    fail "package Kconfig rejected profile: $profile"

[ ! -e "$rtthread_dir" ] || fail "RT-Thread checkout exists: $rtthread_dir"
printf 'RT-Thread ref=%s / BSP=%s / profile=%s\n' \
    "$rtthread_ref" "$bsp_rel" "$profile"
git clone --depth 1 --branch "$rtthread_ref" \
    https://github.com/RT-Thread/rt-thread.git "$rtthread_dir"
[ -f "$bsp_dir/SConstruct" ] || fail 'STM32F407 BSP SConstruct not found'
[ -f "$bsp_dir/rtconfig.h" ] || fail 'STM32F407 BSP rtconfig.h not found'

export RTT_ROOT="$rtthread_dir"
export RTT_CC=gcc
export RTT_EXEC_PATH="$(dirname "$(command -v arm-none-eabi-gcc)")"

# Prevent Env/package-index collisions with the separate upstream infrared
# package. Build the checked-out package via its ORIGINAL SConscript in an
# isolated CI-only source directory, outside automatically scanned packages/.
if grep -Eq '^CONFIG_PKG_USING_INFRARED=y$' "$bsp_dir/.config" || \
    grep -Eq '^#define[[:space:]]+PKG_USING_INFRARED([[:space:]]|$)' "$bsp_dir/rtconfig.h"; then
    fail 'BSP already enables the different upstream infrared package'
fi
ci_pkg="$bsp_dir/ci_infrared"
[ ! -e "$ci_pkg" ] || fail "CI package path already exists: $ci_pkg"
mkdir -p "$ci_pkg"
cp "$package_root/SConscript" "$package_root/Kconfig" "$ci_pkg/"
cp -R "$package_root/inc" "$package_root/src" "$package_root/framework" "$ci_pkg/"

# All selected STM32F407 BSPs already discover subdirectories' SConscript
# through their root SConscript. Loading the package again in SConstruct would
# link the same object twice and cause multiple-definition errors.
[ -f "$bsp_dir/SConscript" ] || fail 'BSP auto-discovery SConscript missing'
grep -Fq "SConscript(os.path.join(d, 'SConscript'))" "$bsp_dir/SConscript" || \
    fail 'BSP SConscript no longer auto-discovers source directories'

if [ "$rtthread_ref" = master ]; then
    # Master STM32F4 HAL/CMSIS is package-managed; .config generation must
    # precede RT-Thread Env package update, otherwise HAL sources are missing.
    env_dir="$ci_dir/env"
    [ ! -e "$env_dir" ] || fail "RT-Thread Env checkout exists: $env_dir"
    git clone --depth 1 --branch master https://github.com/RT-Thread/env.git "$env_dir"
    python3 -m pip install --disable-pip-version-check -e "$env_dir"
    export RTT_ENV="$env_dir"
    (
        cd "$bsp_dir"
        scons --pyconfig-silent
        python3 "$RTT_ENV/env.py" package --upgrade
        python3 "$RTT_ENV/env.py" package --update
    )
    # Env places the real HAL/device CMSIS sources under bsp/stm32/libraries/.
    # The master BSP's SConstruct separately checks only for the existence of
    # board-local packages/ entries, not for headers in those entries.
    stm32_libs="$rtthread_dir/bsp/stm32/libraries"
    [ -f "$bsp_dir/packages/CMSIS-Core-latest/Include/cmsis_gcc.h" ] || \
        fail 'master CMSIS-Core package is missing'
    [ -f "$stm32_libs/stm32f4_hal_driver-latest/Inc/stm32f4xx_hal.h" ] || \
        fail 'master STM32F4 HAL package is missing'
    [ -f "$stm32_libs/stm32f4_cmsis_driver-latest/Include/stm32f4xx.h" ] || \
        fail 'master STM32F4 device CMSIS package is missing'
    for pkg in stm32f4_hal_driver-latest stm32f4_cmsis_driver-latest; do
        entry="$bsp_dir/packages/$pkg"
        # Preserve Env-created directories even when they contain no headers.
        # Only replace a dangling symlink in this disposable CI checkout.
        if [ -L "$entry" ] && [ ! -e "$entry" ]; then
            rm "$entry"
        fi
        if [ ! -e "$entry" ]; then
            mkdir -p "$entry"
        fi
        [ -d "$entry" ] || \
            fail "master BSP package entry is not a directory: $entry"
    done
fi

# Insert Kconfig-resolved macros inside BSP rtconfig.h's final include guard.
rtconfig="$bsp_dir/rtconfig.h"
tail -n 1 "$rtconfig" | grep -Eq '^#endif([[:space:]]|$)' || \
    fail 'rtconfig.h closing guard changed'
rtconfig_tmp="$ci_dir/rtconfig-$profile.h"
sed '$d' "$rtconfig" > "$rtconfig_tmp"
printf '\n/* Infrared CI-only Kconfig profile: %s */\n' "$profile" >> "$rtconfig_tmp"
# RT_USING_PWM without a BSP_USING_PWMn timer creates an empty enum in the
# v4.1.1 STM32 HAL driver. Use the board's Kconfig-supported TIM2 CH4 instance
# for compile/link validation; no hardware is driven by this CI build.
if [ "$rtthread_ref" = v4.1.1 ] && grep -Fxq '#define RT_USING_PWM' "$profile_header"; then
    printf '#define BSP_USING_PWM\n#define BSP_USING_PWM2\n#define BSP_USING_PWM2_CH4\n' \
        >> "$rtconfig_tmp"
fi
cat "$profile_header" >> "$rtconfig_tmp"
printf '\n#endif\n' >> "$rtconfig_tmp"
cp "$rtconfig_tmp" "$rtconfig"
while IFS= read -r macro; do
    grep -Fxq "$macro" "$rtconfig" || fail "missing effective macro: $macro"
done < "$profile_header"

{
    printf 'local_source_sha=%s\n' "${GITHUB_SHA:-local-zip}"
    printf 'rtthread_sha=%s\n' "$(git -C "$rtthread_dir" rev-parse HEAD)"
    if [ "$rtthread_ref" = master ]; then
        printf 'env_sha=%s\n' "$(git -C "$env_dir" rev-parse HEAD)"
    fi
    printf 'compiler=%s\n' "$(arm-none-eabi-gcc -dumpfullversion)"
    printf 'bsp=%s\nprofile=%s\n' "$bsp_rel" "$profile"
} | tee "$revisions"
printf '\nCI effective package macros:\n'
cat "$profile_header"

cd "$bsp_dir"
# Legacy RT-Thread carries its HAL in-tree; master uses fetched packages.
if [ "$rtthread_ref" != master ]; then
    [ -f "$rtthread_dir/bsp/stm32/libraries/STM32F4xx_HAL/SConscript" ] || \
        fail 'legacy STM32F4 HAL BSP library is missing'
fi
# Neither configuration parsing nor SCons source inspection counts as a build.
scons -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')"
elf="$bsp_dir/rt-thread.elf"
[ -s "$elf" ] || fail 'STM32F407 rt-thread.elf was not linked'
arm-none-eabi-size "$elf"
arm-none-eabi-readelf -h "$elf" | grep -Eq 'Machine:[[:space:]]*ARM([[:space:]]|$)' || \
    fail 'linked ELF is not for ARM'
python3 "$matrix" --profile "$profile" --check-objects "$bsp_dir/build"
printf 'INFRARED_STM32_BUILD_PASS profile=%s\n' "$profile"
