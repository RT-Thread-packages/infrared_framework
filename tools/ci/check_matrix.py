#!/usr/bin/env python3
"""Validate infrared Kconfig choices, SConscript source selection and build objects.

The source graph check is static; each CI build additionally compiles and links
an STM32F407 BSP. Only resolved Kconfig values are emitted to its rtconfig.h.
"""

import argparse
import json
import pathlib
import sys
import tempfile
import types
from dataclasses import dataclass, field

ROOT = pathlib.Path(__file__).resolve().parents[2]
PACKAGE = "infrared"


@dataclass(frozen=True)
class Profile:
    mode: str
    requested: dict[str, str] = field(default_factory=dict)
    host: dict[str, str] = field(default_factory=dict)


PROFILES = {
    "legacy-minimal": Profile("legacy", {"PKG_USING_DRV_INFRARED": "n"}),
    "legacy-nec-rx": Profile("legacy", {
        "PKG_USING_DRV_INFRARED": "y", "INFRARED_NEC_DECODER": "y",
        "INFRARED_RECEIVE": "y", "INFRARED_SEND": "n"},
        {"RT_USING_HWTIMER": "y"}),
    "legacy-rx-tx": Profile("legacy", {
        "PKG_USING_DRV_INFRARED": "y", "INFRARED_NEC_DECODER": "y",
        "INFRARED_RECEIVE": "y", "INFRARED_SEND": "y"},
        {"RT_USING_HWTIMER": "y", "RT_USING_PWM": "y"}),
    "framework-core": Profile("framework", {"INFRARED_FRAMEWORK_NEC_CODEC": "n"}),
    "framework-pin-nec": Profile("framework", {
        "INFRARED_DEVICE_PIN": "y", "INFRARED_FRAMEWORK_NEC_CODEC": "y",
        "INFRARED_MANUAL_PIN_CAPACITY": "1"}, {"RT_USING_HWTIMER": "y"}),
    "framework-capture": Profile("framework", {
        "INFRARED_USING_INPUT_CAPTURE": "y", "INFRARED_FRAMEWORK_NEC_CODEC": "y",
        "INFRARED_MANUAL_CAPTURE_CAPACITY": "1"},
        {"RT_USING_INPUT_CAPTURE": "y"}),
    "framework-pwm": Profile("framework", {
        "INFRARED_DEVICE_PWM": "y", "INFRARED_FRAMEWORK_NEC_CODEC": "y",
        "INFRARED_MANUAL_PWM_CAPACITY": "1", "INFRARED_TX_TIMEOUT_TICKS": "1000"},
        {"RT_USING_PWM": "y", "RT_USING_HWTIMER": "y"}),
    "framework-full": Profile("framework", {
        "INFRARED_MAX_DEVICES": "3", "INFRARED_DEVICE_PIN": "y",
        "INFRARED_USING_INPUT_CAPTURE": "y", "INFRARED_DEVICE_PWM": "y",
        "INFRARED_FRAMEWORK_NEC_CODEC": "y", "INFRARED_USING_MSH": "y",
        "INFRARED_USING_EXAMPLE": "y", "INFRARED_USING_THREAD_EXAMPLE": "y",
        "INFRARED_KCONFIG_SLOT_COUNT": "3", "INFRARED_KCONFIG_DEVICE1": "y",
        "INFRARED_KCONFIG_DEVICE2": "y", "INFRARED_DEV0_INPUT_PIN": "y",
        "INFRARED_DEV0_OUTPUT": "y", "INFRARED_DEV1_INPUT_CAPTURE": "y",
        "INFRARED_DEV2_INPUT_NONE": "y", "INFRARED_DEV2_OUTPUT": "y"},
        {"RT_USING_PWM": "y", "RT_USING_HWTIMER": "y",
         "RT_USING_INPUT_CAPTURE": "y"}),
    "framework-sparse": Profile("framework", {
        "INFRARED_DEVICE_PIN": "y", "INFRARED_FRAMEWORK_NEC_CODEC": "y",
        "INFRARED_USING_EXAMPLE": "y", "INFRARED_KCONFIG_SLOT_COUNT": "3",
        "INFRARED_MAX_DEVICES": "3", "INFRARED_KCONFIG_DEVICE0": "n",
        "INFRARED_KCONFIG_DEVICE2": "y", "INFRARED_DEV2_INPUT_PIN": "y",
        "INFRARED_CHECK_ALL": "n", "INFRARED_USING_DIAGNOSTICS": "n"},
        {"RT_USING_HWTIMER": "y"}),
    "framework-pulse-only": Profile("framework", {
        "INFRARED_FRAMEWORK_NEC_CODEC": "n",
        "INFRARED_USING_PULSE_DISTANCE": "y",
        "INFRARED_USING_DIAGNOSTICS": "n"}),
}

# Only master profiles that do not reference the retired HWTIMER API are built.
MASTER_PROFILES = (
    "legacy-minimal", "framework-core", "framework-capture", "framework-pulse-only",
)
RTTHREAD_BUILD_MATRIX = (
    [{"rtthread": "v4.1.1", "profile": profile} for profile in PROFILES] +
    [{"rtthread": "master", "profile": profile} for profile in MASTER_PROFILES]
)

BSP_HOST = {
    "RT_USING_PIN": "y",
    "RT_USING_HEAP": "y",
    "RT_USING_COMPONENTS_INIT": "y",
    "RT_USING_EVENT": "y",
    "FINSH_USING_MSH": "y",
    "RT_USING_PWM": "n",
    "RT_USING_HWTIMER": "n",
    "RT_USING_INPUT_CAPTURE": "n",
    "RT_INPUT_CAPTURE_RB_SIZE": "100",
}

ALL_SOURCES = {
    "src/infrared.c", "src/nec_decoder.c", "src/drv_infrared.c",
    "framework/src/infrared_device.c", "framework/src/infrared_callback.c",
    "framework/src/backends/input/infrared_pin.c",
    "framework/src/backends/input/infrared_capture.c",
    "framework/src/backends/output/infrared_pwm.c",
    "framework/codecs/pulse_distance/pulse_distance.c",
    "framework/codecs/nec/nec_codec.c",
    "framework/codecs/nec/nec_msh.c",
    "framework/examples/infrared_example.c", "framework/examples/infrared_msh.c",
}


def ensure(condition, message):
    if not condition:
        raise AssertionError(message)


def new_kconfig():
    import kconfiglib

    # The BSP provides these host capabilities. Kconfiglib resolves the actual
    # package's choices and dependencies rather than treating requested macros
    # as if they were all simultaneously enabled.
    temp = tempfile.TemporaryDirectory(prefix="infrared-matrix-")
    wrapper = pathlib.Path(temp.name) / "Kconfig"
    lines = ['mainmenu "Infrared CI"']
    for name in BSP_HOST:
        if name == "RT_INPUT_CAPTURE_RB_SIZE":
            lines += [f"config {name}", '    int "CI capture ringbuffer entries"',
                      '    depends on RT_USING_INPUT_CAPTURE',
                      f'    default {BSP_HOST[name]}']
        else:
            lines += [f"config {name}", '    bool "CI host capability"',
                      f'    default {BSP_HOST[name]}']
    lines.append(f'source "{(ROOT / "Kconfig").as_posix()}"')
    wrapper.write_text("\n".join(lines) + "\n", encoding="utf-8")
    try:
        conf = kconfiglib.Kconfig(str(wrapper), warn_to_stderr=False)
    except Exception:
        temp.cleanup()
        raise
    return temp, conf


def resolve(name, profile=None, override_host=None):
    temp, conf = new_kconfig()
    try:
        item = profile or PROFILES[name]
        for symbol, value in {**item.host, **(override_host or {})}.items():
            ensure(symbol in conf.syms, f"unknown host setting: {symbol}")
            conf.syms[symbol].set_value(value)
        conf.syms["PKG_USING_INFRARED"].set_value("y")
        conf.syms["INFRARED_FRAMEWORK" if item.mode == "framework"
                  else "INFRARED_LEGACY"].set_value("y")
        for symbol, value in item.requested.items():
            ensure(symbol in conf.syms, f"unknown package symbol: {symbol}")
            conf.syms[symbol].set_value(value)
        values = {symbol: cfg.str_value for symbol, cfg in conf.syms.items()
                  if symbol}
        warnings = list(conf.warnings)
        types = {symbol: cfg.type for symbol, cfg in conf.syms.items() if symbol}
    finally:
        temp.cleanup()
    return values, warnings, types


def expected_sources(values):
    enabled = lambda name: values.get(name) == "y"
    if enabled("INFRARED_LEGACY"):
        sources = {"src/infrared.c"}
        if enabled("INFRARED_NEC_DECODER"):
            sources.add("src/nec_decoder.c")
        if enabled("PKG_USING_DRV_INFRARED"):
            sources.add("src/drv_infrared.c")
        return sources
    ensure(enabled("INFRARED_FRAMEWORK"), "neither implementation mode is enabled")
    sources = {"framework/src/infrared_device.c",
               "framework/src/infrared_callback.c"}
    selections = {
        "INFRARED_DEVICE_PIN": "framework/src/backends/input/infrared_pin.c",
        "INFRARED_USING_INPUT_CAPTURE": "framework/src/backends/input/infrared_capture.c",
        "INFRARED_DEVICE_PWM": "framework/src/backends/output/infrared_pwm.c",
        "INFRARED_USING_PULSE_DISTANCE": "framework/codecs/pulse_distance/pulse_distance.c",
        "INFRARED_FRAMEWORK_NEC_CODEC": "framework/codecs/nec/nec_codec.c",
        "INFRARED_USING_EXAMPLE": "framework/examples/infrared_example.c",
        "INFRARED_USING_MSH": "framework/examples/infrared_msh.c",
    }
    for option, source in selections.items():
        if enabled(option):
            sources.add(source)
    if enabled("INFRARED_USING_MSH") and enabled("INFRARED_FRAMEWORK_NEC_CODEC"):
        sources.add("framework/codecs/nec/nec_msh.c")
    return sources


def selected_sconscript_sources(values):
    info = {}
    shim = types.ModuleType("building")
    shim.GetCurrentDir = lambda: str(ROOT)
    shim.GetDepend = lambda keys: all(values.get(key) == "y" for key in keys)

    def define_group(name, files, **kwargs):
        info.update(name=name, sources=files, kwargs=kwargs)
        return info

    shim.DefineGroup = define_group
    shim.Return = lambda _: None
    previous = sys.modules.get("building")
    sys.modules["building"] = shim
    try:
        exec(compile((ROOT / "SConscript").read_bytes(), "SConscript", "exec"),
             {"__builtins__": __builtins__}, {})
    finally:
        if previous is None:
            del sys.modules["building"]
        else:
            sys.modules["building"] = previous
    ensure(info.get("name") == "Infrared_frame", "unexpected SConscript group")
    ensure(info["kwargs"].get("depend") == ["PKG_USING_INFRARED"],
           "SConscript package gating changed")
    return set(info["sources"])


def verify(name):
    values, warnings, types = resolve(name)
    item = PROFILES[name]
    ensure(values.get("PKG_USING_INFRARED") == "y", "package was not enabled")
    ensure((values.get("INFRARED_LEGACY") == "y") !=
           (values.get("INFRARED_FRAMEWORK") == "y"),
           "LEGACY and FRAMEWORK were not mutually exclusive")
    ensure(values["INFRARED_FRAMEWORK" if item.mode == "framework"
                  else "INFRARED_LEGACY"] == "y", "requested mode not selected")
    for symbol, requested in item.requested.items():
        ensure(values.get(symbol) == requested,
               f"{name}: {symbol}: requested {requested}, resolved {values.get(symbol)}")
    for symbol, requested in item.host.items():
        ensure(values.get(symbol) == requested,
               f"{name}: host {symbol}: requested {requested}, resolved {values.get(symbol)}")
    ensure(not warnings, f"{name}: Kconfig warnings: {warnings}")
    macros = render_header(values, types)
    ensure(not any(line.endswith(' ""') for line in macros),
           f"{name}: inactive Kconfig value exported as empty macro")
    if values.get("INFRARED_NEC_DECODER") == "y":
        ensure(f"#define NEC_DEVIATION {values['NEC_DEVIATION']}" in macros,
               f"{name}: legacy NEC tolerance not exported")
    if values.get("RT_USING_INPUT_CAPTURE") == "y":
        ensure(f"#define RT_INPUT_CAPTURE_RB_SIZE {values['RT_INPUT_CAPTURE_RB_SIZE']}" in macros,
               f"{name}: Input Capture ringbuffer size not exported")
    selected = selected_sconscript_sources(values)
    expected = expected_sources(values)
    ensure(selected == expected,
           f"{name}: SConscript={sorted(selected)}, expected={sorted(expected)}")
    for source in selected:
        ensure((ROOT / source).is_file(), f"selected source missing: {source}")
    if values.get("INFRARED_CHECK_ALL") == "n" and item.mode == "framework":
        for check in ("PARAMETERS", "CONFIG", "RESOURCE", "DEVICE"):
            ensure(values.get("INFRARED_CHECK_" + check) == "n",
                   "master diagnostics gate failed to disable " + check)
    if values.get("INFRARED_FRAMEWORK_NEC_CODEC") == "y":
        ensure(values.get("INFRARED_USING_PULSE_DISTANCE") == "y",
               "NEC did not select pulse-distance helper")
    if item.mode == "framework" and values.get("INFRARED_USING_EXAMPLE") == "y":
        cap = int(values["INFRARED_MAX_DEVICES"])
        slots = int(values["INFRARED_KCONFIG_SLOT_COUNT"])
        enabled_slots = [i for i in range(3) if values.get(f"INFRARED_KCONFIG_DEVICE{i}") == "y"]
        ensure(all(i < slots and i < cap for i in enabled_slots),
               "configured example index is outside declared capacity")
    return {"profile": name, "mode": item.mode, "status": "PASS",
            "sources": sorted(selected),
            "resolved": {k: v for k, v in sorted(values.items())
                         if k.startswith("INFRARED_") or k in
                         ("PKG_USING_INFRARED", "PKG_USING_DRV_INFRARED")}}


def render_header(values, types):
    import kconfiglib

    # Emit only active, typed Kconfig values; inactive INT/STRING symbols have
    # empty str_value and must NOT mask the framework's numeric defaults.
    export = {key: value for key, value in values.items()
              if key.startswith("INFRARED_") or key in
              ("NEC_DEVIATION", "PKG_USING_INFRARED", "PKG_USING_DRV_INFRARED",
               "RT_USING_PWM", "RT_USING_HWTIMER", "RT_USING_INPUT_CAPTURE",
               "RT_INPUT_CAPTURE_RB_SIZE")}
    lines = ["/* Generated from package Kconfig by infrared CI; do not edit. */"]
    for key, value in sorted(export.items()):
        kind = types[key]
        if not value or value == "n":
            continue
        if kind in (kconfiglib.BOOL, kconfiglib.TRISTATE):
            ensure(value == "y", f"invalid enabled boolean {key}={value}")
            lines.append(f"#define {key}")
        elif kind == kconfiglib.INT:
            ensure(value.lstrip("-").isdigit(), f"invalid integer {key}={value!r}")
            lines.append(f"#define {key} {value}")
        elif kind == kconfiglib.HEX:
            ensure(value.lower().startswith("0x"), f"invalid hex {key}={value!r}")
            lines.append(f"#define {key} {value}")
        elif kind == kconfiglib.STRING:
            lines.append(f"#define {key} {json.dumps(value, ensure_ascii=False)}")
        else:
            raise AssertionError(f"invalid Kconfig symbol type: {key}={kind}")
    return lines


def emit_header(values, types, path):
    lines = render_header(values, types)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def emit_sources(values, path):
    sources = sorted(selected_sconscript_sources(values))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(sources) + "\n", encoding="utf-8")


def check_objects(name, build_dir):
    sources = set(verify(name)["sources"])
    wanted = {pathlib.Path(p).stem + ".o" for p in sources}
    all_known = {pathlib.Path(p).stem + ".o" for p in ALL_SOURCES}
    ensure(len(all_known) == len(ALL_SOURCES), "object names are ambiguous")
    root = pathlib.Path(build_dir)
    ensure(root.is_dir(), f"BSP object directory not found: {root}")
    found = {}
    for obj in root.rglob("*.o"):
        if obj.name not in all_known:
            continue
        # Do not accidentally count similarly named RT-Thread BSP objects.
        if obj.as_posix().find("/ci_infrared/") < 0:
            continue
        found.setdefault(obj.name, []).append(str(obj))
    ensure(set(found) == wanted,
           f"selected objects differ: missing={sorted(wanted-set(found))}, "
           f"unexpected={sorted(set(found)-wanted)}")
    for obj, paths in found.items():
        ensure(len(paths) == 1, f"{obj}: compiled {len(paths)} times: {paths}")
    return sorted(found)


def negative_cases():
    for name, profile, host_override, option in [
        ("no-heap-capture", PROFILES["framework-capture"],
         {"RT_USING_HEAP": "n"}, "INFRARED_USING_INPUT_CAPTURE"),
        ("no-hwtimer-pin", PROFILES["framework-pin-nec"],
         {"RT_USING_HWTIMER": "n"}, "INFRARED_DEVICE_PIN"),
        ("no-pwm-backend", PROFILES["framework-pwm"],
         {"RT_USING_PWM": "n"}, "INFRARED_DEVICE_PWM"),
    ]:
        values, _, _ = resolve(name, profile, host_override)
        ensure(values.get(option) == "n", f"{name}: {option} should be unavailable")
    return 3


def source_graph_only():
    # Lightweight offline check (without Kconfiglib), not a replacement for
    # CI's parsed Kconfig validation.
    for name, profile in PROFILES.items():
        vals = {**BSP_HOST, **profile.host, "PKG_USING_INFRARED": "y",
                "INFRARED_LEGACY": "y" if profile.mode == "legacy" else "n",
                "INFRARED_FRAMEWORK": "y" if profile.mode == "framework" else "n",
                **profile.requested}
        if profile.mode == "framework" and "INFRARED_FRAMEWORK_NEC_CODEC" not in vals:
            vals["INFRARED_FRAMEWORK_NEC_CODEC"] = "y"
        if vals.get("INFRARED_FRAMEWORK_NEC_CODEC") == "y":
            vals["INFRARED_USING_PULSE_DISTANCE"] = "y"
        actual = selected_sconscript_sources(vals)
        ensure(actual == expected_sources(vals), f"static source graph mismatch: {name}")
        ensure(all((ROOT / p).is_file() for p in actual), name + ": source missing")
    return len(PROFILES)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list-profiles", action="store_true")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--source-graph-only", action="store_true")
    parser.add_argument("--profile", choices=sorted(PROFILES))
    parser.add_argument("--emit-header", type=pathlib.Path)
    parser.add_argument("--emit-sources", type=pathlib.Path)
    parser.add_argument("--check-objects", type=pathlib.Path)
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    if args.list_profiles:
        print("\n".join(PROFILES))
        return 0
    if args.source_graph_only:
        print(f"PASS static SConscript selection: {source_graph_only()} profiles (no compile)")
        return 0
    if not args.all and not args.profile:
        parser.error("select --all, --profile or --source-graph-only")
    if (args.emit_header or args.emit_sources or args.check_objects) and not args.profile:
        parser.error("--emit-header, --emit-sources and --check-objects require --profile")
    try:
        report = [verify(name) for name in (PROFILES if args.all else (args.profile,))]
        if args.all:
            negative = negative_cases()
            print(f"PASS Kconfig negative dependency cases: {negative}")
        if args.emit_header or args.emit_sources:
            values, _, types = resolve(args.profile)
            if args.emit_header:
                emit_header(values, types, args.emit_header)
            if args.emit_sources:
                emit_sources(values, args.emit_sources)
        if args.check_objects:
            objs = check_objects(args.profile, args.check_objects)
            print(f"PASS actual package objects ({len(objs)}): {', '.join(objs)}")
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False)
                                   + "\n", encoding="utf-8")
        for item in report:
            print(f"PASS Kconfig + SConscript: {item['profile']}, {len(item['sources'])} sources")
    except (AssertionError, ImportError, OSError, ValueError) as exc:
        print(f"INFRARED_MATRIX_FAIL: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
