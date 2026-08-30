#!/bin/bash
# Build BurrTools on macOS into build/ (meson + ninja), the same as a
# "rebuild the binaries" compile. Run from anywhere:
#   ./build_scripts/build-macos.sh
set -euo pipefail

usage() {
	cat <<'EOF'
Build BurrTools on macOS into the build/ directory (burrtools, burrTxt, burrTxt2).

Usage: build-macos.sh [options]

Options:
  --buildtype TYPE     Meson build type: release, debug, debugoptimized, minsize, plain
                       (default: keep the existing build, or Meson project defaults)
  --build-dir DIR      Build directory (default: build)
  --skip-dep-install   Do not install Homebrew packages; only check and warn
  -h, --help           Show this help

Missing tools are installed with Homebrew when possible (meson, ninja, cmake,
pkg-config, libpng). Xcode Command Line Tools must already be installed:
  xcode-select --install

To make a .app / DMG after this, run build_scripts/create-macos-bundle.sh
EOF
}

BUILDTYPE=
BUILD_DIR=build
SKIP_DEP_INSTALL=0

while [ $# -gt 0 ]; do
	case "$1" in
		--buildtype)
			BUILDTYPE="${2:?--buildtype requires an argument}"
			shift 2
			;;
		--buildtype=*)
			BUILDTYPE="${1#*=}"
			shift
			;;
		--build-dir)
			BUILD_DIR="${2:?--build-dir requires an argument}"
			shift 2
			;;
		--build-dir=*)
			BUILD_DIR="${1#*=}"
			shift
			;;
		--skip-dep-install)
			SKIP_DEP_INSTALL=1
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "Unknown option: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

if [ -n "$BUILDTYPE" ]; then
	case "$BUILDTYPE" in
		release|debug|debugoptimized|minsize|plain) ;;
		*)
			echo "Unsupported --buildtype '$BUILDTYPE'." >&2
			exit 2
			;;
	esac
fi

if [ "$(uname -s)" != Darwin ]; then
	echo "This script builds BurrTools on macOS." >&2
	echo "On Linux, run build_scripts/build-linux.sh" >&2
	echo "On Windows, run build_scripts/build-windows.bat" >&2
	exit 1
fi

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
cd "$ROOT"

# Apple Silicon and Intel Homebrew prefixes.
export PATH="/opt/homebrew/bin:/usr/local/bin:${PATH}"

info() { printf '%s\n' "$*"; }
warn() { printf 'Warning: %s\n' "$*" >&2; }
die() { printf 'Error: %s\n' "$*" >&2; exit 1; }

have_cmd() { command -v "$1" >/dev/null 2>&1; }

version_ge() {
	python3 -c '
import sys
a = [int(x) for x in sys.argv[1].split(".")[:3]]
b = [int(x) for x in sys.argv[2].split(".")[:3]]
a += [0] * (3 - len(a))
b += [0] * (3 - len(b))
sys.exit(0 if tuple(a) >= tuple(b) else 1)
' "$1" "$2"
}

print_manual_deps() {
	cat <<'EOF'
Install Xcode Command Line Tools and Homebrew packages:

  xcode-select --install
  brew install meson ninja cmake pkg-config libpng

Meson fetches FLTK from subprojects/ on first configure (needs git and cmake).
EOF
}

ensure_xcode() {
	if have_cmd c++ && have_cmd git; then
		return 0
	fi
	warn "C++ compiler or git is missing. Install Xcode Command Line Tools:"
	info "  xcode-select --install"
	die "Install the Command Line Tools and re-run this script."
}

brew_has() {
	brew list --formula "$1" >/dev/null 2>&1
}

ensure_deps() {
	ensure_xcode

	if [ "$SKIP_DEP_INSTALL" -eq 1 ]; then
		return 0
	fi

	if ! have_cmd brew; then
		warn "Homebrew is not installed (https://brew.sh)."
		print_manual_deps
		return 0
	fi

	local pkg
	local missing=
	for pkg in meson ninja cmake pkg-config libpng; do
		if ! brew_has "$pkg" && ! have_cmd "$pkg"; then
			# pkg-config is provided by the pkgconf formula on recent Homebrew.
			if [ "$pkg" = pkg-config ] && brew_has pkgconf; then
				continue
			fi
			missing="$missing $pkg"
		fi
	done
	missing=${missing# }

	if [ -n "$missing" ]; then
		info "Installing with Homebrew: $missing"
		# shellcheck disable=SC2086
		brew install $missing
	fi
}

check_tools() {
	local tool
	local missing=
	for tool in meson ninja cmake git c++; do
		if ! have_cmd "$tool"; then
			missing="$missing $tool"
		fi
	done
	if ! have_cmd python3; then
		missing="$missing python3"
	fi
	if ! have_cmd pkg-config && ! have_cmd pkgconf; then
		missing="$missing pkg-config"
	fi
	missing=${missing# }
	if [ -n "$missing" ]; then
		print_manual_deps
		die "Missing tools on PATH: $missing"
	fi

	local meson_ver
	meson_ver=$(meson --version)
	if ! version_ge "$meson_ver" 0.56; then
		die "meson $meson_ver is older than 0.56. Run: brew upgrade meson"
	fi
	info "Using meson $meson_ver"
}

ensure_deps
check_tools

VERSION=$(git describe --tags --always --dirty 2>/dev/null || true)
[ -n "$VERSION" ] || VERSION=dev
if [ -n "$BUILDTYPE" ]; then
	info "Building BurrTools $VERSION ($BUILDTYPE) in $BUILD_DIR"
else
	info "Building BurrTools $VERSION in $BUILD_DIR"
fi

if [ -n "$BUILDTYPE" ]; then
	if [ -f "$BUILD_DIR/build.ninja" ]; then
		meson setup "$BUILD_DIR" --reconfigure --buildtype="$BUILDTYPE"
	else
		meson setup "$BUILD_DIR" --buildtype="$BUILDTYPE"
	fi
elif [ ! -f "$BUILD_DIR/build.ninja" ]; then
	meson setup "$BUILD_DIR"
fi

meson compile -C "$BUILD_DIR"

for bin in burrtools burrTxt burrTxt2; do
	[ -f "$BUILD_DIR/$bin" ] || die "Build finished but $BUILD_DIR/$bin is missing."
done

info "Binaries:"
info "  $BUILD_DIR/burrtools"
info "  $BUILD_DIR/burrTxt"
info "  $BUILD_DIR/burrTxt2"
info "Run the GUI with: $BUILD_DIR/burrtools"
