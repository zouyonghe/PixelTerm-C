#!/usr/bin/env bash
set -euo pipefail

REPO_SLUG="zouyonghe/PixelTerm-C"
INSTALL_DIR="/usr/local/bin"
DRY_RUN=0
PRINT_ASSET_NAME=0
PRINT_INSTALL_PATH=0
TMP_DIR=""

usage() {
  cat <<'EOF'
Install PixelTerm-C from the latest GitHub Release.

Usage:
  bash install.sh [options]

Options:
  --bin-dir <dir>       Install pixelterm into the given directory
  --dry-run             Print the resolved download URL and destination
  --print-asset-name    Print the detected release asset name and exit
  --print-install-path  Print the final install path and exit
  --help                Show this help message

Examples:
  curl -fsSL https://raw.githubusercontent.com/zouyonghe/PixelTerm-C/main/scripts/install.sh | bash
  curl -fsSL https://raw.githubusercontent.com/zouyonghe/PixelTerm-C/main/scripts/install.sh | bash -s -- --bin-dir "$HOME/.local/bin"
EOF
}

log() {
  printf '%s\n' "$*"
}

die() {
  printf 'Error: %s\n' "$*" >&2
  exit 1
}

cleanup_tmp_dir() {
  if [ -n "${TMP_DIR}" ] && [ -d "${TMP_DIR}" ]; then
    rm -rf "${TMP_DIR}"
  fi
}

detect_uname_s() {
  if [ -n "${PIXELTERM_INSTALL_UNAME_S:-}" ]; then
    printf '%s\n' "${PIXELTERM_INSTALL_UNAME_S}"
    return
  fi

  uname -s
}

detect_uname_m() {
  if [ -n "${PIXELTERM_INSTALL_UNAME_M:-}" ]; then
    printf '%s\n' "${PIXELTERM_INSTALL_UNAME_M}"
    return
  fi

  uname -m
}

detect_platform() {
  printf '%s %s\n' "$(detect_os_name)" "$(detect_arch_name)"
}

detect_os_name() {
  local uname_s

  uname_s="$(detect_uname_s)"

  case "${uname_s}" in
    Linux)
      printf 'linux\n'
      ;;
    Darwin)
      printf 'macos\n'
      ;;
    *)
      die "Unsupported operating system: ${uname_s}"
      ;;
  esac
}

detect_arch_name() {
  local uname_m

  uname_m="$(detect_uname_m)"

  case "${uname_m}" in
    x86_64|amd64)
      printf 'amd64\n'
      ;;
    arm64|aarch64)
      printf 'arm64\n'
      ;;
    *)
      die "Unsupported architecture: ${uname_m}"
      ;;
  esac
}

release_asset_name() {
  local os_name
  local arch_name

  os_name="$(detect_os_name)"
  arch_name="$(detect_arch_name)"

  printf 'pixelterm-%s-%s\n' "${arch_name}" "${os_name}"
}

download_url() {
  local asset_name
  asset_name="$(release_asset_name)"
  printf 'https://github.com/%s/releases/latest/download/%s\n' "${REPO_SLUG}" "${asset_name}"
}

install_path() {
  printf '%s/pixelterm\n' "${INSTALL_DIR%/}"
}

run_privileged() {
  if [ "$#" -eq 0 ]; then
    return 0
  fi

  if "$@"; then
    return 0
  fi

  if command -v sudo >/dev/null 2>&1; then
    sudo "$@"
    return 0
  fi

  die "Install target is not writable and sudo is not available"
}

download_binary() {
  local url
  local destination

  url="$(download_url)"
  destination="$1"

  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "${url}" -o "${destination}"
    return 0
  fi

  if command -v wget >/dev/null 2>&1; then
    wget -qO "${destination}" "${url}"
    return 0
  fi

  die "curl or wget is required to download PixelTerm-C"
}

prepare_install_dir() {
  local parent_dir

  if [ -d "${INSTALL_DIR}" ]; then
    return 0
  fi

  parent_dir="$(dirname "${INSTALL_DIR}")"
  run_privileged mkdir -p "${INSTALL_DIR}"

  if [ ! -d "${INSTALL_DIR}" ]; then
    die "Failed to create install directory under ${parent_dir}"
  fi
}

install_binary() {
  local downloaded_file
  local destination

  downloaded_file="$1"
  destination="$(install_path)"

  prepare_install_dir
  run_privileged install -m 0755 "${downloaded_file}" "${destination}"
}

parse_args() {
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --bin-dir)
        [ "$#" -ge 2 ] || die "Missing value for --bin-dir"
        INSTALL_DIR="$2"
        shift 2
        ;;
      --dry-run)
        DRY_RUN=1
        shift
        ;;
      --print-asset-name)
        PRINT_ASSET_NAME=1
        shift
        ;;
      --print-install-path)
        PRINT_INSTALL_PATH=1
        shift
        ;;
      --help|-h)
        usage
        exit 0
        ;;
      *)
        die "Unknown argument: $1"
        ;;
    esac
  done
}

main() {
  local asset_name
  local url
  local destination
  local tmp_file
  local os_name
  local arch_name

  parse_args "$@"

  if [ "${PRINT_ASSET_NAME}" -eq 1 ]; then
    release_asset_name
    exit 0
  fi

  if [ "${PRINT_INSTALL_PATH}" -eq 1 ]; then
    install_path
    exit 0
  fi

  asset_name="$(release_asset_name)"
  url="$(download_url)"
  destination="$(install_path)"
  os_name="$(detect_os_name)"
  arch_name="$(detect_arch_name)"

  if [ "${DRY_RUN}" -eq 1 ]; then
    log "Resolved platform: ${os_name}/${arch_name}"
    log "Release asset: ${asset_name}"
    log "Download URL: ${url}"
    log "Install destination: ${destination}"
    exit 0
  fi

  TMP_DIR="$(mktemp -d)"
  tmp_file="${TMP_DIR}/${asset_name}"

  log "Downloading ${asset_name}..."
  download_binary "${tmp_file}"

  log "Installing to ${destination}..."
  install_binary "${tmp_file}"

  log "PixelTerm-C installed to ${destination}"

  if [ "${os_name}" = "macos" ]; then
    log "If macOS blocks the binary, run: xattr -dr com.apple.quarantine ${destination}"
  fi
}

trap cleanup_tmp_dir EXIT

main "$@"
