from __future__ import annotations

import platform
import shutil
import subprocess
import time
from argparse import Namespace
from pathlib import Path

from . import helper

# the standalone backend integration tests and the environment each one needs.
# the vdo mysql/pgsql drivers load their native client library through ffi, so the loader path is added below.
_DB_COMPOSE = ["docker", "compose", "-f", "tests/docker-compose.yml"]
_DB_CONTAINERS = (
    "varn-test-backends-redis-1",
    "varn-test-backends-mysql-1",
    "varn-test-backends-postgres-1",
)
_DB_ENV = {
    "VARN_REDIS_HOST": "127.0.0.1",
    "VARN_REDIS_PORT": "6379",
    "VARN_MYSQL_HOST": "127.0.0.1",
    "VARN_MYSQL_PORT": "3306",
    "VARN_MYSQL_USER": "root",
    "VARN_MYSQL_PASS": "varnpass",
    "VARN_MYSQL_DB": "varntest",
    "VDO_MYSQL_DSN": "mysql:host=127.0.0.1;port=3306;dbname=varntest",
    "VDO_MYSQL_USER": "root",
    "VDO_MYSQL_PASS": "varnpass",
    "VDO_PGSQL_DSN": "pgsql:host=127.0.0.1;port=5432;dbname=varntest",
    "VDO_PGSQL_USER": "varn",
    "VDO_PGSQL_PASS": "varnpass",
}
_DB_TESTS = (
    "components/redis/tests/integration.lua",
    "components/mysql/tests/integration.lua",
    "components/vdo/tests/integration.lua",
    "components/scheduler/tests/scheduler_test.lua",
)
# the ai adapters are fully covered by the deterministic mock tests in the cross-platform suite, so this real-api
# smoke test is opt-in via --ai-live and each provider is skipped unless its *_API_KEY is in the environment.
_DB_AI_LIVE_TEST = "components/ai/tests/live_test.lua"

# common windows exception codes a crashing process reports as its exit status, so a failure caused by
# a crash is told apart from a normal non-zero exit or an assertion.
_WINDOWS_EXIT_CODES = {
    0xC0000005: "access violation",
    0xC00000FD: "stack overflow",
    0xC0000374: "heap corruption",
    0xC0000409: "stack buffer overrun",
    0xC000013A: "interrupted (ctrl-c)",
}


def _describe_exit(code: int) -> str:
    # subprocess reports a posix signal as a negative code and a windows exception as a large unsigned one.
    if code < 0:
        return f"exit {code} (killed by signal {-code})"

    description = _WINDOWS_EXIT_CODES.get(code & 0xFFFFFFFF)
    if description:
        return f"exit {code} (0x{code & 0xFFFFFFFF:08X} {description})"

    return f"exit {code}"


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

    # component tests with no external dependency run alongside the module tests; ones needing a database
    # or server (scheduler and vdo over sqlite/ffi, redis, mysql) are run separately where that backend exists
    for relative in (
        "components/ai/tests/mock_test.lua",
        "components/ai/tests/adapters_test.lua",
        "components/validate/tests/integration.lua",
        "components/retry/tests/integration.lua",
        "components/pool/tests/integration.lua",
        "components/test/tests/integration.lua",
        "components/vdo/tests/sql_test.lua",
        "components/vdo/tests/dsn_test.lua",
        "components/env/tests/integration.lua",
    ):
        candidate = helper.PROJECT_DIR / relative
        if candidate.exists():
            tests.append(candidate)

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

        print(f"FAIL  {relative}  [{_describe_exit(result.returncode)}]")
        output = (result.stdout + result.stderr).strip().splitlines()
        if not output:
            print("      (no output — the process produced nothing before exiting, typical of a crash)")
        # keep enough tail to hold a crash summary plus its backtrace.
        for line in output[-40:]:
            print(f"      {line}")
        failed.append(relative)

    if scratch.exists():
        shutil.rmtree(scratch)

    print(f"\n{passed} passed, {len(failed)} failed")
    if failed:
        raise SystemExit(1)


def _client_library_path() -> dict[str, str]:
    # the vdo mysql/pgsql drivers dlopen libmysqlclient/libpq, so point the loader at the client libraries and their openssl dependency.
    if platform.system() == "Darwin":
        dirs = [d for d in (
            "/opt/homebrew/opt/mysql-client/lib",
            "/opt/homebrew/opt/libpq/lib",
            "/opt/homebrew/opt/openssl@3/lib",
        ) if Path(d).exists()]
        if dirs:
            return {"DYLD_LIBRARY_PATH": ":".join(dirs)}
    return {}


def _wait_for_healthy(containers: tuple[str, ...], attempts: int = 60) -> bool:
    for _ in range(attempts):
        states = []
        for name in containers:
            probe = subprocess.run(
                ["docker", "inspect", "--format", "{{.State.Health.Status}}", name],
                capture_output=True,
                text=True,
            )
            states.append(probe.stdout.strip())
        if all(state == "healthy" for state in states):
            return True
        time.sleep(2)
    return False


def run_db(args: Namespace) -> None:
    build_dir = args.build_dir or "build"
    binary = _binary(build_dir, "varn")
    if not binary.exists():
        print(f"varn binary not found under {build_dir}/bin; build first with: python3 varn.py build")
        raise SystemExit(1)

    if getattr(args, "down", False):
        helper.run(_DB_COMPOSE + ["down", "-v"])
        return

    helper.run(_DB_COMPOSE + ["up", "-d"])
    print("waiting for redis, mysql and postgres to become healthy...")
    if not _wait_for_healthy(_DB_CONTAINERS):
        print("the backend services did not become healthy in time")
        raise SystemExit(1)

    scratch = helper.PROJECT_DIR / build_dir / "test-scratch-db"
    backend_env = {**_DB_ENV, **_client_library_path()}

    suite = list(_DB_TESTS)
    if getattr(args, "ai_live", False):
        suite.append(_DB_AI_LIVE_TEST)

    passed = 0
    failed: list[str] = []
    for relative in suite:
        test = helper.PROJECT_DIR / relative
        if not test.exists():
            continue

        if scratch.exists():
            shutil.rmtree(scratch)
        scratch.mkdir(parents=True)

        env = helper.environ_with(VARN_TEST_DIR=str(scratch), **backend_env)
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

        print(f"FAIL  {relative}  [{_describe_exit(result.returncode)}]")
        for line in (result.stdout + result.stderr).strip().splitlines()[-40:]:
            print(f"      {line}")
        failed.append(relative)

    if scratch.exists():
        shutil.rmtree(scratch)

    print(f"\n{passed} passed, {len(failed)} failed")
    print("stop the backends with: python3 varn.py test-db --down")
    if failed:
        raise SystemExit(1)
