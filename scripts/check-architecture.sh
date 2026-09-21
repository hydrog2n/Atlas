#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
cd "${repo_root}"

bad=0

if ! grep -q "atlas-domain" .DESIGN/adr/ADR-0001-technology-baseline.md; then
  echo "Missing atlas-domain boundary in ADR-0001" >&2
  bad=1
fi

if ! test -f .DESIGN/decision-backlog.md; then
  echo "Missing decision backlog" >&2
  bad=1
fi

if ! grep -q "v0.1.0" README.md ROADMAP.md .DESIGN/Atlas_Master_Design_Specification_Revision_3_0.md; then
  echo "Release baseline not traceable across project docs" >&2
  bad=1
fi

if [ "$bad" -ne 0 ]; then
  exit 1
fi

echo "Architecture governance checks passed."
