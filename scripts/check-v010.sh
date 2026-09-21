#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
cd "${repo_root}"

if command -v cmake >/dev/null 2>&1; then
  echo "cmake: $(cmake --version | head -n 1)"
else
  echo "cmake: missing"
fi

if command -v ninja >/dev/null 2>&1; then
  echo "ninja: $(ninja --version)"
else
  echo "ninja: missing"
fi

cmake --preset headless-vcpkg
cmake --build --preset headless-vcpkg
ctest --preset headless-vcpkg --output-on-failure
