#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$script_dir/run-routing-smoke.sh"
"$script_dir/run-capacity-aware-smoke.sh"
"$script_dir/run-task-smoke.sh"
"$script_dir/run-diagnostics-smoke.sh"
"$script_dir/run-topology-smoke.sh"
python3 "$script_dir/run-link-metrics-smoke.py"

echo "SatCompute smoke suites passed."
