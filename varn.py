from __future__ import annotations

import argparse

from tools.core import android, apple, cxx, general, tests, wasm


def _common(
    parser: argparse.ArgumentParser, default_build_dir: str | None = None
) -> None:
    parser.add_argument(
        "--build-dir", default=default_build_dir, help="build directory"
    )

    parser.add_argument(
        "--config", default="Release", help="CMake build type (default: Release)"
    )


def _opt_build(parser: argparse.ArgumentParser) -> None:
    _common(parser, "build")
    parser.add_argument("--arch", help="CMAKE_OSX_ARCHITECTURES override (e.g. x86_64)")
    parser.add_argument(
        "-D",
        "--define",
        action="append",
        default=[],
        metavar="VAR=VALUE",
        help="extra -D cache entry forwarded to cmake (repeatable)",
    )


def _opt_android(parser: argparse.ArgumentParser) -> None:
    _common(parser)
    parser.add_argument(
        "--api", type=int, default=24, help="Android min SDK (default: 24)"
    )


def _opt_wasm(parser: argparse.ArgumentParser) -> None:
    _common(parser)
    parser.add_argument("--zip", help="ON or OFF")


def _opt_test(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--build-dir", default="build", help="build directory holding bin/varn (default: build)"
    )


def _opt_test_cpp(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--build-dir", default="build/test", help="build directory for the C++ tests (default: build/test)"
    )
    parser.add_argument(
        "--config", default="Release", help="CMake build type (default: Release)"
    )
    parser.add_argument(
        "-D",
        "--define",
        action="append",
        default=[],
        metavar="VAR=VALUE",
        help="extra -D cache entry forwarded to cmake (repeatable)",
    )


def _opt_clean(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--build-dir", default="build", help="build directory (default: build)"
    )


# task -> (handler, options configurator, one-line help shown in --help).
_TASKS = {
    "build": (
        cxx.build,
        _opt_build,
        "Build the native varn executable for the current desktop OS.",
    ),
    "test": (
        tests.run,
        _opt_test,
        "Run the Lua test suite against the built varn binary.",
    ),
    "test-cpp": (
        tests.run_cpp,
        _opt_test_cpp,
        "Build and run the native C++ test target (googletest).",
    ),
    "apple": (apple.build, _common, "Build Varn.xcframework for all Apple slices."),
    "android": (
        android.build,
        _opt_android,
        "Build the Android .aar (libvarn.so for every ABI).",
    ),
    "wasm": (wasm.build, _opt_wasm, "Build the varn_wasm target with Emscripten."),
    "app-wasm": (
        wasm.app,
        _opt_wasm,
        "Build wasm, then bundle the browser app into apps/wasm/dist.",
    ),
    "serve": (
        wasm.serve,
        _opt_wasm,
        "Build wasm, then start the Vite dev server for apps/wasm.",
    ),
    "format": (
        general.fmt,
        None,
        "Run clang-format over the modules/ and src/ sources.",
    ),
    "clean": (general.clean, _opt_clean, "Remove the build directory."),
    "zip": (general.zip, None, "Create a source archive beside the repo."),
}


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="varn", description=__doc__)
    tasks = parser.add_subparsers(dest="task", metavar="task", required=True)

    for name, (_, configure, help_text) in _TASKS.items():
        task = tasks.add_parser(name, help=help_text, description=help_text)
        if configure:
            configure(task)

    return parser


def main() -> None:
    args = _build_parser().parse_args()
    _TASKS[args.task][0](args)


if __name__ == "__main__":
    main()
