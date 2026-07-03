preset := env_var_or_default("TROWEL_PRESET", if os() == "macos" { "macos-debug" } else { "linux-debug" })
build_dir := "build" / preset
app := build_dir / "trowel.app/Contents/MacOS/trowel"
bin := if os() == "macos" { app } else { build_dir / "trowel" }

default:
    @just --list

# Configure the CMake build (idempotent)
configure:
    cmake --preset {{preset}}

# Build the current preset
build: configure
    cmake --build --preset {{preset}}

# Rebuild from scratch
rebuild: clean build

# Run the app
run: build
    {{bin}}

editor: run

# Run headless (offscreen Qt platform) — useful for smoke tests
run-offscreen: build
    {{bin}} -platform offscreen

# Run the smoke test suite (pytest under offscreen Qt)
smoke: build
    cd tests/smoke && pytest -q

# Delete the current preset's build directory
clean:
    rm -rf {{build_dir}}

# Delete all build directories
clean-all:
    rm -rf build

# Format C++ sources (requires clang-format)
fmt:
    find src -name '*.cpp' -o -name '*.h' -o -name '*.mm' | xargs clang-format -i

# Lint check without modifying (CI-friendly)
fmt-check:
    find src -name '*.cpp' -o -name '*.h' -o -name '*.mm' | xargs clang-format --dry-run --Werror

# Static analysis via clang-tidy (needs compile_commands.json — configure first)
tidy: configure
    find src -name '*.cpp' | xargs clang-tidy -p {{build_dir}}

# Print resolved preset and paths (debug helper)
info:
    @echo "preset:    {{preset}}"
    @echo "build_dir: {{build_dir}}"
    @echo "bin:       {{bin}}"

# One-time macOS dev-env fix: Homebrew Qt 6.10.1 references a missing binary
fix-homebrew-qt:
    #!/usr/bin/env bash
    set -euo pipefail
    stub="/opt/homebrew/opt/qt/bin/wasmdeployqt"
    if [[ ! -e "$stub" ]]; then
        touch "$stub" && chmod +x "$stub"
        echo "stubbed $stub"
    else
        echo "$stub already present"
    fi
