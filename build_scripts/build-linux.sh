#!/bin/bash
# Build BurrTools on Linux: install missing packages when possible, compile, and
# pack a release tarball. Run from anywhere:
#   ./build_scripts/build-linux.sh
#   ./build_scripts/build-linux.sh --skip-dep-install --buildtype debug
set -euo pipefail

usage() {
	cat <<'EOF'
Build BurrTools on Linux (meson + ninja) and create a release tarball.

Usage: build-linux.sh [options]

Options:
  --buildtype TYPE     Meson build type: release (default) or debug
  --build-dir DIR      Build directory (default: build)
  --skip-dep-install   Do not install packages; only check and warn
  --skip-package       Compile only; do not create a tarball
  -h, --help           Show this help

Dependencies are installed with the distro package manager when possible
(apt, dnf, yum, pacman, zypper). You will be prompted for sudo if needed.
EOF
}

BUILDTYPE=release
BUILD_DIR=build
SKIP_DEP_INSTALL=0
SKIP_PACKAGE=0

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
		--skip-package)
			SKIP_PACKAGE=1
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

case "$BUILDTYPE" in
	release|debug|debugoptimized|minsize|plain) ;;
	*)
		echo "Unsupported --buildtype '$BUILDTYPE'." >&2
		exit 2
		;;
esac

if [ "$(uname -s)" != Linux ]; then
	echo "This script builds BurrTools on Linux." >&2
	echo "On macOS, run build_scripts/build-macos.sh" >&2
	echo "On Windows, run build_scripts/build-windows.bat" >&2
	exit 1
fi

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
cd "$ROOT"

info() { printf '%s\n' "$*"; }
warn() { printf 'Warning: %s\n' "$*" >&2; }
die() { printf 'Error: %s\n' "$*" >&2; exit 1; }

have_cmd() { command -v "$1" >/dev/null 2>&1; }

version_ge() {
	# Return 0 if $1 >= $2 (dotted numeric versions).
	local newer
	newer=$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n1)
	[ "$newer" = "$1" ]
}

run_root() {
	if [ "$(id -u)" -eq 0 ]; then
		"$@"
	elif have_cmd sudo; then
		sudo "$@"
	else
		return 1
	fi
}

PKG_KIND=

detect_pkg_kind() {
	if [ -r /etc/os-release ]; then
		# shellcheck disable=SC1091
		. /etc/os-release
	fi
	local id="${ID:-}"
	local like="${ID_LIKE:-}"
	case "$id" in
		debian|ubuntu|linuxmint|pop|elementary|raspbian|kali)
			PKG_KIND=apt
			return
			;;
		fedora|rhel|centos|rocky|almalinux|ol)
			PKG_KIND=dnf
			return
			;;
		arch|manjaro|endeavouros|cachyos)
			PKG_KIND=pacman
			return
			;;
		opensuse*|sles)
			PKG_KIND=zypper
			return
			;;
	esac
	case "$like" in
		*debian*|*ubuntu*) PKG_KIND=apt ;;
		*fedora*|*rhel*|*centos*) PKG_KIND=dnf ;;
		*arch*) PKG_KIND=pacman ;;
		*suse*) PKG_KIND=zypper ;;
		*) PKG_KIND= ;;
	esac
	if [ "$PKG_KIND" = dnf ] && ! have_cmd dnf && have_cmd yum; then
		PKG_KIND=yum
	fi
}

apt_installed() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'install ok installed'; }
rpm_installed() { rpm -q "$1" >/dev/null 2>&1; }
pacman_installed() { pacman -Qi "$1" >/dev/null 2>&1; }

filter_missing() {
	# Usage: filter_missing <testfn> pkg [pkg...]
	# Prints packages that are not installed.
	local testfn=$1
	shift
	local pkg
	for pkg in "$@"; do
		if ! "$testfn" "$pkg"; then
			printf '%s\n' "$pkg"
		fi
	done
}

install_packages() {
	local kind=$1
	shift
	[ $# -eq 0 ] && return 0
	info "Installing packages: $*"
	case "$kind" in
		apt)
			run_root apt-get update
			run_root apt-get install -y "$@"
			;;
		dnf)
			run_root dnf install -y "$@"
			;;
		yum)
			run_root yum install -y "$@"
			;;
		pacman)
			run_root pacman -S --needed --noconfirm "$@"
			;;
		zypper)
			run_root zypper --non-interactive install -y "$@"
			;;
		*)
			return 1
			;;
	esac
}

print_manual_deps() {
	cat <<'EOF'
Could not install packages automatically. Install a C++ toolchain plus:

  meson (>= 0.56)  ninja  cmake  pkg-config  git  python3
  OpenGL + GLU headers  libpng  zlib

Optional (faster configure; otherwise Meson builds FLTK from subprojects/):
  FLTK 1.3+ development packages
  X11 / Xft / Xinerama / Xcursor  (needed when building FLTK from source)

Debian/Ubuntu:
  sudo apt-get install -y build-essential cmake ninja-build meson pkg-config git python3 \
    libgl-dev libglu1-mesa-dev libpng-dev zlib1g-dev libfltk1.3-dev \
    libx11-dev libxext-dev libxft-dev libxinerama-dev libxcursor-dev libxrender-dev libfontconfig1-dev

Fedora:
  sudo dnf install -y gcc-c++ cmake ninja-build meson pkgconf git python3 \
    mesa-libGL-devel mesa-libGLU-devel libpng-devel zlib-devel fltk-devel \
    libX11-devel libXext-devel libXft-devel libXinerama-devel libXcursor-devel libXrender-devel fontconfig-devel

Arch:
  sudo pacman -S --needed base-devel cmake ninja meson pkgconf git python glu libpng zlib fltk
EOF
}

install_meson_pip() {
	if ! have_cmd python3; then
		return 1
	fi
	info "Installing meson with pip (user install)..."
	if python3 -m pip install --user meson >/dev/null 2>&1 || \
	   python3 -m pip install --user --break-system-packages meson >/dev/null 2>&1; then
		export PATH="${HOME}/.local/bin:${PATH}"
		return 0
	fi
	return 1
}

ensure_deps() {
	detect_pkg_kind

	local missing=()
	local optional=()

	case "$PKG_KIND" in
		apt)
			missing=$(filter_missing apt_installed \
				build-essential cmake ninja-build pkg-config git python3 \
				libgl-dev libglu1-mesa-dev libpng-dev zlib1g-dev)
			optional=$(filter_missing apt_installed \
				meson python3-pip libfltk1.3-dev \
				libx11-dev libxext-dev libxft-dev libxinerama-dev \
				libxcursor-dev libxrender-dev libfontconfig1-dev)
			;;
		dnf|yum)
			missing=$(filter_missing rpm_installed \
				gcc-c++ cmake ninja-build pkgconf git python3 \
				mesa-libGL-devel mesa-libGLU-devel libpng-devel zlib-devel)
			optional=$(filter_missing rpm_installed \
				meson python3-pip fltk-devel \
				libX11-devel libXext-devel libXft-devel libXinerama-devel \
				libXcursor-devel libXrender-devel fontconfig-devel)
			;;
		pacman)
			missing=$(filter_missing pacman_installed \
				gcc cmake ninja pkgconf git python glu libpng zlib)
			optional=$(filter_missing pacman_installed meson fltk)
			;;
		zypper)
			missing=$(filter_missing rpm_installed \
				gcc-c++ cmake ninja meson pkg-config git python3 \
				Mesa-libGL-devel glu-devel libpng-devel zlib-devel)
			optional=$(filter_missing rpm_installed \
				fltk-devel \
				libX11-devel libXext-devel libXft-devel libXinerama-devel \
				libXcursor-devel libXrender-devel fontconfig-devel)
			;;
		*)
			missing=
			optional=
			;;
	esac

	# Word-split is intentional: these are package-name lists.
	# shellcheck disable=SC2086
	set -- $missing
	local req_list=("$@")
	# shellcheck disable=SC2086
	set -- $optional
	local opt_list=("$@")

	if [ "$SKIP_DEP_INSTALL" -eq 1 ]; then
		if [ ${#req_list[@]} -gt 0 ] || [ ${#opt_list[@]} -gt 0 ]; then
			warn "Missing packages (not installing because --skip-dep-install): ${req_list[*]} ${opt_list[*]}"
		fi
		return 0
	fi

	if [ -z "$PKG_KIND" ]; then
		warn "No supported package manager detected."
		print_manual_deps
		return 0
	fi

	if [ ${#req_list[@]} -gt 0 ]; then
		if ! install_packages "$PKG_KIND" "${req_list[@]}"; then
			warn "Could not install required packages."
			print_manual_deps
			die "Install the packages above and re-run this script."
		fi
	fi

	if [ ${#opt_list[@]} -gt 0 ]; then
		if ! install_packages "$PKG_KIND" "${opt_list[@]}"; then
			warn "Optional packages were not installed (${opt_list[*]}). Configure may still succeed."
		fi
	fi
}

check_tools() {
	local tool
	local missing=()
	for tool in meson ninja cmake git c++; do
		if ! have_cmd "$tool"; then
			missing+=("$tool")
		fi
	done
	if ! have_cmd python3 && ! have_cmd python; then
		missing+=(python3)
	fi
	if ! have_cmd pkg-config && ! have_cmd pkgconf; then
		missing+=(pkg-config)
	fi
	if [ ${#missing[@]} -gt 0 ]; then
		print_manual_deps
		die "Missing tools on PATH: ${missing[*]}"
	fi

	local meson_ver
	meson_ver=$(meson --version)
	if ! version_ge "$meson_ver" 0.56; then
		warn "meson $meson_ver is older than 0.56 (needed for the FLTK CMake subproject)."
		if [ "$SKIP_DEP_INSTALL" -eq 0 ]; then
			install_meson_pip || die "Please install meson >= 0.56 (pip install --user meson)."
			meson_ver=$(meson --version)
			version_ge "$meson_ver" 0.56 || die "meson is still too old ($meson_ver)."
		else
			die "Upgrade meson or re-run without --skip-dep-install."
		fi
	fi
	info "Using meson $meson_ver"
}

ensure_deps
check_tools

VERSION=$(git describe --tags --always --dirty 2>/dev/null || true)
[ -n "$VERSION" ] || VERSION=dev
info "Building BurrTools $VERSION ($BUILDTYPE) in $BUILD_DIR"

if [ -f "$BUILD_DIR/build.ninja" ]; then
	meson setup "$BUILD_DIR" --reconfigure --buildtype="$BUILDTYPE"
else
	meson setup "$BUILD_DIR" --buildtype="$BUILDTYPE"
fi

meson compile -C "$BUILD_DIR"

for bin in burrtools burrTxt burrTxt2; do
	[ -f "$BUILD_DIR/$bin" ] || die "Build finished but $BUILD_DIR/$bin is missing."
done

if have_cmd strip; then
	strip "$BUILD_DIR/burrtools" "$BUILD_DIR/burrTxt" "$BUILD_DIR/burrTxt2" || true
fi

info "Binaries:"
info "  $BUILD_DIR/burrtools"
info "  $BUILD_DIR/burrTxt"
info "  $BUILD_DIR/burrTxt2"

if [ "$SKIP_PACKAGE" -eq 1 ]; then
	exit 0
fi

ARCH=$(uname -m)
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
DEST="$STAGE/burrtools"
mkdir -p "$DEST"
cp "$BUILD_DIR/burrtools" "$BUILD_DIR/burrTxt" "$BUILD_DIR/burrTxt2" "$DEST/"
[ -d examples ] && cp -R examples "$DEST/examples"
[ -f README.md ] && cp README.md "$DEST/"
[ -f COPYING ] && cp COPYING "$DEST/"

ARCHIVE="burrtools-${VERSION}-linux-${ARCH}.tar.gz"
tar -C "$STAGE" -czf "$ARCHIVE" burrtools
info "Created $ARCHIVE"
info "Run the GUI with: $BUILD_DIR/burrtools"
