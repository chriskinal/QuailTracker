#!/usr/bin/env bash
#
# Generates the QuailTracker-specific TFLite Micro source tree at
# stm32/QuailTracker_U575/Middlewares/Third_Party/TFLiteMicro/
#
# What gets generated is gitignored — it's a heavy (~hundreds of MB)
# transitive checkout we never want in the repo. This script
# reproduces the manual steps from the TFLiteMicro/README.md so CI
# (and any new contributor) can rebuild the tree deterministically.
#
# Pinned commits below — bump when upstream moves and we want it.
#
# Usage:
#   ./tools/fetch_tflite_micro.sh
#
# Requires: bash, git, wget, unzip, python3, GNU make >= 3.82, and the
# PlatformIO ARM GCC toolchain at ~/.platformio/packages/toolchain-gccarmnoneeabi/.
# CI installs all of those before calling this script. Locally on
# macOS, system make (3.81) is too old — `brew install make wget`.

set -euo pipefail

# ───────────────────────── Pinned upstream refs ─────────────────────────
# Tracked dependencies. Bump these consciously — the generated tree is
# part of the firmware ABI surface.
TFLITE_MICRO_COMMIT="51bee03bed4776f1de88dd87226ff8c260f88e3c"
GEMMLOWP_COMMIT="719139ce755a0f31cbf1c37f7f98adcc7fc9f425"
RUY_COMMIT="d37128311b445e758136b8602d1bbd2a755e115d"
EYALROZ_PRINTF_COMMIT="f8ed5a9bd9fa8384430973465e94aa14c925872d"

# ─────────────────────────── Path resolution ────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TARGET_DIR="$REPO_ROOT/stm32/QuailTracker_U575/Middlewares/Third_Party/TFLiteMicro"

PIO_TOOLCHAIN="${PIO_TOOLCHAIN:-$HOME/.platformio/packages/toolchain-gccarmnoneeabi}"
if [[ ! -d "$PIO_TOOLCHAIN/bin" ]]; then
    echo "ERROR: PlatformIO ARM GCC toolchain not found at $PIO_TOOLCHAIN" >&2
    echo "Install PlatformIO and run 'pio platform install ststm32' first," >&2
    echo "or set PIO_TOOLCHAIN=/path/to/toolchain." >&2
    exit 1
fi

# Pick `gmake` over `make` if it exists (handles macOS where system make is 3.81).
if command -v gmake >/dev/null 2>&1; then
    MAKE_BIN="gmake"
elif command -v make >/dev/null 2>&1; then
    MAKE_VERSION="$(make --version | head -1 | awk '{print $NF}')"
    MAKE_MAJOR="${MAKE_VERSION%%.*}"
    if (( MAKE_MAJOR < 4 )); then
        echo "ERROR: GNU make >= 3.82 required, found $MAKE_VERSION." >&2
        echo "On macOS: brew install make (then re-run; this script will pick gmake)." >&2
        exit 1
    fi
    MAKE_BIN="make"
else
    echo "ERROR: GNU make not found." >&2
    exit 1
fi

# ─────────────────────────── Skip if up to date ─────────────────────────
STAMP="$TARGET_DIR/.fetch_stamp"
EXPECTED_STAMP="tflm=$TFLITE_MICRO_COMMIT gemmlowp=$GEMMLOWP_COMMIT ruy=$RUY_COMMIT printf=$EYALROZ_PRINTF_COMMIT"
if [[ -f "$STAMP" && "$(cat "$STAMP")" == "$EXPECTED_STAMP" ]]; then
    echo "TFLiteMicro tree is already at pinned commits — nothing to do."
    exit 0
fi

echo "Fetching TFLite Micro deps to $TARGET_DIR ..."

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# ─────────────────────── Clone tflite-micro at pin ──────────────────────
git clone https://github.com/tensorflow/tflite-micro.git "$WORK_DIR/tflite-micro"
git -C "$WORK_DIR/tflite-micro" checkout --quiet "$TFLITE_MICRO_COMMIT"

cd "$WORK_DIR/tflite-micro"
DOWNLOADS="tensorflow/lite/micro/tools/make/downloads"
mkdir -p "$DOWNLOADS"

# ────────────── Third-party deps (using TFLM's helper scripts) ──────────
# These pin their own commits internally; trust them.
bash tensorflow/lite/micro/tools/make/flatbuffers_download.sh "$DOWNLOADS"
bash tensorflow/lite/micro/tools/make/kissfft_download.sh "$DOWNLOADS"

# ──────────── Manual deps (no upstream helper scripts exist) ────────────
fetch_archive() {
    local url="$1"
    local dest_name="$2"
    local tmp="$WORK_DIR/${dest_name}.zip"
    local extract_root="$WORK_DIR/${dest_name}_extract"
    wget --quiet -O "$tmp" "$url"
    mkdir -p "$extract_root"
    unzip -q "$tmp" -d "$extract_root"
    # Archive contains a single top-level <name>-<sha> dir; rename it.
    mv "$extract_root"/*/ "$DOWNLOADS/$dest_name/"
}

fetch_archive "https://github.com/google/gemmlowp/archive/${GEMMLOWP_COMMIT}.zip" "gemmlowp"
fetch_archive "https://github.com/google/ruy/archive/${RUY_COMMIT}.zip"             "ruy"
fetch_archive "https://github.com/eyalroz/printf/archive/${EYALROZ_PRINTF_COMMIT}.zip" "eyalroz_printf"

# ───────────── Generate the QuailTracker source tree ────────────────────
PATH="$(brew --prefix make 2>/dev/null)/libexec/gnubin:$PATH" \
python3 tensorflow/lite/micro/tools/project_generation/create_tflm_tree.py \
    --makefile_options="TARGET=cortex_m_generic OPTIMIZED_KERNEL_DIR=cmsis_nn TARGET_ARCH=cortex-m33 TARGET_TOOLCHAIN_ROOT=$PIO_TOOLCHAIN/bin/" \
    --rename_cc_to_cpp \
    "$TARGET_DIR"

echo "$EXPECTED_STAMP" > "$STAMP"
echo "Done. Generated tree at $TARGET_DIR"
