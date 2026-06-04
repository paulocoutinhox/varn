from __future__ import annotations

import shutil
import subprocess
from argparse import Namespace
from pathlib import Path

from . import helper


def _binary(build_dir: str, stem: str) -> Path:
    base = helper.PROJECT_DIR / build_dir / "bin"
    for name in (stem, f"{stem}.exe"):
        candidate = base / name
        if candidate.exists():
            return candidate
    return base / stem


def run_cpp(args: Namespace) -> None:
    build_dir = args.build_dir or "build/test"

    configure = ["cmake", "-B", build_dir, "-S", ".", f"-DCMAKE_BUILD_TYPE={args.config}", "-DVARN_BUILD_TESTS=ON"]
    for define in args.define:
        configure.append(f"-D{define}")
    helper.run(configure)

    helper.run(["cmake", "--build", build_dir, "--target", "varn_tests", "-j", str(helper.jobs())])
    helper.run([str(_binary(build_dir, "varn_tests"))])


def run(args: Namespace) -> None:
    build_dir = args.build_dir or "build"
    binary = _binary(build_dir, "varn")
    if not binary.exists():
        print(f"varn binary not found under {build_dir}/bin; build first with: python3 varn.py build")
        raise SystemExit(1)

    tests = sorted((helper.PROJECT_DIR / "modules").glob("*/lua/tests/*.lua"))
    if not tests:
        print("no lua tests found under modules/*/lua/tests")
        raise SystemExit(1)

    # each test receives a fresh scratch directory through VARN_TEST_DIR, created and cleaned here.
    scratch = helper.PROJECT_DIR / build_dir / "test-scratch"

    passed = 0
    failed: list[Path] = []
    for test in tests:
        relative = test.relative_to(helper.PROJECT_DIR)

        if scratch.exists():
            shutil.rmtree(scratch)
        scratch.mkdir(parents=True)

        env = helper.environ_with(VARN_TEST_DIR=str(scratch))
        result = subprocess.run(
            [str(binary), str(test)],
            cwd=str(helper.PROJECT_DIR),
            capture_output=True,
            text=True,
            env=env,
        )

        if result.returncode == 0:
            print(f"PASS  {relative}")
            passed += 1
            continue

        print(f"FAIL  {relative}")
        output = (result.stdout + result.stderr).strip().splitlines()
        for line in output[-12:]:
            print(f"      {line}")
        failed.append(relative)

    if scratch.exists():
        shutil.rmtree(scratch)

    print(f"\n{passed} passed, {len(failed)} failed")
    if failed:
        raise SystemExit(1)
