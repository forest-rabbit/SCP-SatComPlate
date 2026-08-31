#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$script_dir/run-full-routing-regression.sh"
"$script_dir/run-full-workload-regression.sh"
"$script_dir/run-fault-lifecycle-regression.sh"
"$script_dir/run-n4b-joint-acceptance.sh"

echo "SatCompute regression suites passed."
