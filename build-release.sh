#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# build-release.sh -- Build drogon-blueprint in Release mode with maximum
#                     compiler optimizations and mimalloc allocator support.
#
# Flags applied via CMakeLists.txt for Release builds:
#   -O3            Full optimization (vectorisation, inlining, unrolling)
#   -march=native  Target the build machine's CPU feature set
#   -flto          Link-Time Optimization across all translation units
#
# Allocator strategy (automatic fallback):
#   1. Link mimalloc via vcpkg (override feature -- replaces malloc globally)
#   2. If mimalloc is not found at configure time, build without it and
#      provide an LD_PRELOAD wrapper script for runtime injection.
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}"
BUILD_DIR="${PROJECT_ROOT}/build-release"
VCPKG_ROOT="${VCPKG_ROOT:-/home/uday/vcpkg}"
VCPKG_TOOLCHAIN="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
BINARY_NAME="drogon-blueprint"
NPROC="$(nproc 2>/dev/null || echo 4)"

# Colours (no-op if not a terminal)
if [ -t 1 ]; then
    GREEN='\033[0;32m'; YELLOW='\033[1;33m'; RED='\033[0;31m'; NC='\033[0m'
else
    GREEN=''; YELLOW=''; RED=''; NC=''
fi

info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# ---------------------------------------------------------------------------
# Pre-flight checks
# ---------------------------------------------------------------------------
if [ ! -f "${VCPKG_TOOLCHAIN}" ]; then
    error "vcpkg toolchain not found at ${VCPKG_TOOLCHAIN}"
    error "Set VCPKG_ROOT to your vcpkg installation directory."
    exit 1
fi

if ! command -v cmake &>/dev/null; then
    error "cmake is not installed."
    exit 1
fi

if ! command -v ninja &>/dev/null; then
    warn "ninja not found -- falling back to Unix Makefiles."
    GENERATOR="Unix Makefiles"
else
    GENERATOR="Ninja"
fi

# ---------------------------------------------------------------------------
# Configure
# ---------------------------------------------------------------------------
info "Configuring Release build in ${BUILD_DIR}"
info "Generator: ${GENERATOR}"
info "vcpkg toolchain: ${VCPKG_TOOLCHAIN}"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -G "${GENERATOR}" \
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_TOOLCHAIN}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_MIMALLOC=ON \
    2>&1

# Detect mimalloc linkage after build (set below, after link step)

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
info "Building with ${NPROC} parallel jobs ..."
cmake --build "${BUILD_DIR}" --config Release -j "${NPROC}" 2>&1

if [ ! -f "${BUILD_DIR}/${BINARY_NAME}" ]; then
    error "Build failed -- binary not found at ${BUILD_DIR}/${BINARY_NAME}"
    exit 1
fi

info "Build successful: ${BUILD_DIR}/${BINARY_NAME}"

# ---------------------------------------------------------------------------
# Binary info
# ---------------------------------------------------------------------------
BINARY_SIZE=$(du -h "${BUILD_DIR}/${BINARY_NAME}" | cut -f1)
info "Binary size: ${BINARY_SIZE}"

# Detect mimalloc: check for mi_malloc/mi_free symbols in the binary (works
# for both static and dynamic linkage).  With LTO, symbols may be local (t)
# or global (T), so we match both cases.
# Note: We use a subshell to avoid pipefail issues -- nm + grep -q can cause
# SIGPIPE on large binaries when grep exits early after finding a match.
MIMALLOC_LINKED=false
if (nm "${BUILD_DIR}/${BINARY_NAME}" 2>/dev/null | grep -cE " [Tt] mi_malloc$" >/dev/null 2>&1); then
    MIMALLOC_LINKED=true
    info "Allocator: mimalloc (statically linked -- malloc/free overridden)"
elif (ldd "${BUILD_DIR}/${BINARY_NAME}" 2>/dev/null | grep -c mimalloc >/dev/null 2>&1); then
    MIMALLOC_LINKED=true
    info "Allocator: mimalloc (dynamically linked)"
elif (strings "${BUILD_DIR}/${BINARY_NAME}" 2>/dev/null | grep -c "^mimalloc:" >/dev/null 2>&1); then
    MIMALLOC_LINKED=true
    info "Allocator: mimalloc (detected via embedded strings)"
fi

# ---------------------------------------------------------------------------
# LD_PRELOAD fallback: create a run wrapper if mimalloc is not linked
# ---------------------------------------------------------------------------
RUN_SCRIPT="${BUILD_DIR}/run-${BINARY_NAME}.sh"

if [ "${MIMALLOC_LINKED}" = true ]; then
    # Simple run script -- mimalloc is already linked in
    cat > "${RUN_SCRIPT}" <<'RUNEOF'
#!/usr/bin/env bash
# Run drogon-blueprint (mimalloc linked at build time)
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${DIR}/drogon-blueprint" "$@"
RUNEOF
    chmod +x "${RUN_SCRIPT}"
    info "Run script: ${RUN_SCRIPT}"
else
    warn "mimalloc was NOT linked at build time."
    info "Creating LD_PRELOAD wrapper for runtime mimalloc injection."

    # Try to locate libmimalloc.so
    MIMALLOC_SO=""
    for candidate in \
        "/usr/lib/libmimalloc.so" \
        "/usr/lib/x86_64-linux-gnu/libmimalloc.so" \
        "/usr/local/lib/libmimalloc.so" \
        "${VCPKG_ROOT}/installed/x64-linux/lib/libmimalloc.so" \
        "${BUILD_DIR}/vcpkg_installed/x64-linux/lib/libmimalloc.so"; do
        if [ -f "${candidate}" ]; then
            MIMALLOC_SO="${candidate}"
            break
        fi
    done

    # Also search with find as a last resort
    if [ -z "${MIMALLOC_SO}" ]; then
        MIMALLOC_SO=$(find /usr /home/uday/vcpkg "${BUILD_DIR}" \
            -name "libmimalloc*.so*" -print -quit 2>/dev/null || true)
    fi

    if [ -n "${MIMALLOC_SO}" ]; then
        info "Found mimalloc shared library: ${MIMALLOC_SO}"
    else
        warn "libmimalloc.so not found on this system."
        warn "Install mimalloc:  sudo apt install libmimalloc-dev  OR  vcpkg install mimalloc"
        MIMALLOC_SO="/usr/lib/libmimalloc.so"  # placeholder
    fi

    cat > "${RUN_SCRIPT}" <<RUNEOF
#!/usr/bin/env bash
# Run drogon-blueprint with mimalloc via LD_PRELOAD
# mimalloc was not available at build time, so we inject it at runtime.
set -euo pipefail
DIR="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"

MIMALLOC_LIB="${MIMALLOC_SO}"

if [ -f "\${MIMALLOC_LIB}" ]; then
    echo "[INFO] Using mimalloc via LD_PRELOAD: \${MIMALLOC_LIB}"
    export LD_PRELOAD="\${MIMALLOC_LIB}"
    export MIMALLOC_VERBOSE=1
else
    echo "[WARN] mimalloc not found at \${MIMALLOC_LIB} -- running with system allocator"
fi

exec "\${DIR}/drogon-blueprint" "\$@"
RUNEOF
    chmod +x "${RUN_SCRIPT}"
    info "LD_PRELOAD run script: ${RUN_SCRIPT}"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "============================================================"
echo "  Release Build Complete"
echo "============================================================"
echo "  Binary:     ${BUILD_DIR}/${BINARY_NAME}"
echo "  Size:       ${BINARY_SIZE}"
if [ "${MIMALLOC_LINKED}" = true ]; then
echo "  Allocator:  mimalloc (linked)"
else
echo "  Allocator:  mimalloc (LD_PRELOAD) or system default"
fi
echo "  Flags:      -O3 -march=native -flto"
echo "  Standard:   C++20"
echo ""
echo "  Run:        ${RUN_SCRIPT}"
echo "  Or:         ${BUILD_DIR}/${BINARY_NAME}"
echo "============================================================"
