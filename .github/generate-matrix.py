#!/usr/bin/env python3
#
# Copyright (c) 2026 Michael Vandeberg
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#
# Official repository: https://github.com/cppalliance/capy
#

"""
Generate CI matrix JSON for GitHub Actions.

Reads compilers.json and outputs a JSON array of matrix entries to stdout.
Each entry has fields matching what the ci.yml build job expects.

Usage:
    python3 generate-matrix.py                    # JSON array
    python3 generate-matrix.py | python3 -m json.tool  # pretty-printed
"""

import json
import os
import sys


def load_compilers(path=None):
    if path is None:
        path = os.path.join(os.path.dirname(__file__), "compilers.json")
    with open(path) as f:
        return json.load(f)


def make_entry(compiler_family, spec, **overrides):
    """Build a matrix entry dict from a compiler spec and optional overrides."""
    entry = {
        "compiler": compiler_family,
        "version": spec["version"],
        "cxxstd": spec["cxxstd"],
        "latest-cxxstd": spec["latest_cxxstd"],
        "runs-on": spec["runs_on"],
        "b2-toolset": spec["b2_toolset"],
        "shared": True,
        "build-type": "Release",
        "build-cmake": True,
    }

    if spec.get("container"):
        entry["container"] = spec["container"]
    if spec.get("cxx"):
        entry["cxx"] = spec["cxx"]
    if spec.get("cc"):
        entry["cc"] = spec["cc"]
    if spec.get("generator"):
        entry["generator"] = spec["generator"]
    if spec.get("generator_toolset"):
        entry["generator-toolset"] = spec["generator_toolset"]
    if spec.get("is_latest"):
        entry["is-latest"] = True
    if spec.get("build_cmake") is False:
        entry["build-cmake"] = False
    if spec.get("cmake_cxxstd"):
        entry["cmake-cxxstd"] = spec["cmake_cxxstd"]
    if spec.get("cxxflags"):
        entry["cxxflags"] = spec["cxxflags"]

    entry.update(overrides)
    entry["name"] = generate_name(compiler_family, entry)
    return entry


def generate_name(compiler_family, entry):
    """Generate a human-readable job name from entry fields."""
    name_map = {
        "gcc": "GCC",
        "clang": "Clang",
        "msvc": "MSVC",
        "mingw": "MinGW",
        "clang-cl": "Clang-CL",
        "apple-clang": "Apple-Clang",
    }
    prefix = name_map.get(compiler_family, compiler_family)

    version = entry["version"]
    if version != "*":
        prefix = f"{prefix} {version}"

    standards = entry["cxxstd"].split(",")
    cxxstd = ",".join(f"C++{s}" for s in standards)

    modifiers = []

    runner = entry["runs-on"]
    if "arm" in runner:
        modifiers.append("arm64")
    elif compiler_family == "apple-clang":
        # Extract macOS version from runner name
        macos_ver = runner.replace("macos-", "macOS ")
        modifiers.append(macos_ver)

    if entry.get("asan") and entry.get("ubsan"):
        modifiers.append("asan+ubsan")
    elif entry.get("asan"):
        modifiers.append("asan")
    elif entry.get("ubsan"):
        modifiers.append("ubsan")

    if entry.get("coverage"):
        modifiers.append("coverage")

    if entry.get("x86"):
        modifiers.append("x86")

    if entry.get("shared") is False:
        modifiers.append("static")

    suffix = f" ({', '.join(modifiers)})" if modifiers else ""
    return f"{prefix}: {cxxstd}{suffix}"


def generate_sanitizer_variant(compiler_family, spec):
    """Generate ASAN+UBSAN variant for the latest compiler in a family.

    MSVC does not support UBSAN; only ASAN is enabled for MSVC.
    """
    overrides = {
        "asan": True,
        "build-type": "RelWithDebInfo",
        "shared": True,
    }

    # MSVC and Clang-CL only support ASAN, not UBSAN
    if compiler_family not in ("msvc", "clang-cl"):
        overrides["ubsan"] = True

    if compiler_family == "clang":
        overrides["shared"] = False

    return make_entry(compiler_family, spec, **overrides)


def generate_coverage_variant(compiler_family, spec):
    """Generate coverage variant (GCC only)."""
    return make_entry(compiler_family, spec, **{
        "coverage": True,
        "shared": False,
        "build-type": "Debug",
        "cxxflags": "--coverage -fprofile-arcs -ftest-coverage",
        "ccflags": "--coverage -fprofile-arcs -ftest-coverage",
        "install": "lcov wget unzip",
    })


def generate_x86_variant(compiler_family, spec):
    """Generate x86 (32-bit) variant (Clang only)."""
    return make_entry(compiler_family, spec,
        x86=True,
        shared=False,
        install="gcc-multilib g++-multilib")


def generate_arm_entry(compiler_family, spec):
    """Generate ARM64 variant for a compiler spec."""
    arm_runner = spec["runs_on"].replace("ubuntu-24.04", "ubuntu-24.04-arm")
    # ARM runners don't support containers — build a spec copy without container
    arm_spec = {k: v for k, v in spec.items() if k != "container"}
    arm_spec["runs_on"] = arm_runner
    return make_entry(compiler_family, arm_spec)


def main():
    compilers = load_compilers()
    matrix = []

    for family, specs in compilers.items():
        for spec in specs:
            # Base entry (x86_64 / default arch)
            matrix.append(make_entry(family, spec))

            # ARM entry if supported
            if spec.get("arm"):
                matrix.append(generate_arm_entry(family, spec))

            # Variants for the latest compiler in each family
            if spec.get("is_latest"):
                matrix.append(generate_sanitizer_variant(family, spec))

                if family == "gcc":
                    matrix.append(generate_coverage_variant(family, spec))

                if family == "clang":
                    matrix.append(generate_x86_variant(family, spec))

    json.dump(matrix, sys.stdout)


if __name__ == "__main__":
    main()
