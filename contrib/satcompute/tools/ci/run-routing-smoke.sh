#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly script_dir
repository_root="$(cd "${script_dir}/../../../.." && pwd)"
readonly repository_root

cd "${repository_root}"
export PYTHONDONTWRITEBYTECODE=1

announce()
{
  printf '\n[CI:routing-smoke] %s\n' "$1"
}

announce "topology-only default output"
./waf --run-no-build "satcompute --simulationDuration=110" \
  | tee /tmp/satcompute-ci-topology-only.log
grep -Fq "  discovered : 12" /tmp/satcompute-ci-topology-only.log
grep -Fq "  selected   : 12" /tmp/satcompute-ci-topology-only.log
grep -Fq "  updates    : 11" /tmp/satcompute-ci-topology-only.log
grep -Fq "[TOPO:Update] @ 110s" /tmp/satcompute-ci-topology-only.log
grep -Fq "  satellites : 66" /tmp/satcompute-ci-topology-only.log
grep -Fq "  ISLs       : 132" /tmp/satcompute-ci-topology-only.log
grep -Fq "  run status          : COMPLETE" \
  /tmp/satcompute-ci-topology-only.log
grep -Fq "  output root         : /tmp/satcompute-output" \
  /tmp/satcompute-ci-topology-only.log
if grep -Fq "network-flow-metrics.csv" \
  /tmp/satcompute-ci-topology-only.log; then
  echo "default console still lists individual metric files"
  exit 1
fi
test -f /tmp/satcompute-output/run-summary.json
test ! -e /tmp/satcompute-output/diagnostics
python3 - <<'PY'
import json
from pathlib import Path

summary = json.loads(Path("/tmp/satcompute-output/run-summary.json").read_text())
assert summary["mode"] == "topology-only"
assert summary["run_status"] == "COMPLETE"
assert summary["task_count"] == 0
assert summary["transfer_count"] == 0
assert summary["flow_monitor_tx_packets"] == 0
assert summary["flow_monitor_rx_packets"] == 0
assert summary["flow_monitor_lost_packets"] == 0
PY

announce "static Hash repeat"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-static \
  --simulationDuration=3 \
  --transferTrace=contrib/satcompute/input/traffic/test/diamond-4-static-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=verbose \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-static-a"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-static \
  --simulationDuration=3 \
  --transferTrace=contrib/satcompute/input/traffic/test/diamond-4-static-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=verbose \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-static-b"

announce "dynamic Hash route epochs"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-dynamic \
  --simulationDuration=6 \
  --transferTrace=contrib/satcompute/input/traffic/test/diamond-4-dynamic-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=1024 \
  --islMtuBytes=1500 \
  --islQueueBytes=1500000 \
  --transferLogMode=verbose \
  --routingMode=global-hash-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-dynamic"

announce "stable HRW dynamic epochs"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-hrw-dynamic \
  --simulationDuration=7 \
  --transferTrace=contrib/satcompute/input/traffic/test/diamond-4-hrw-dynamic-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=64000 \
  --islMtuBytes=65535 \
  --islQueueBytes=64000000 \
  --transferLogMode=silent \
  --routingMode=global-hrw-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-hrw-seed1-a"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-hrw-dynamic \
  --simulationDuration=7 \
  --transferTrace=contrib/satcompute/input/traffic/test/diamond-4-hrw-dynamic-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=64000 \
  --islMtuBytes=65535 \
  --islQueueBytes=64000000 \
  --transferLogMode=silent \
  --routingMode=global-hrw-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-hrw-seed1-b"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-hrw-dynamic \
  --simulationDuration=7 \
  --transferTrace=contrib/satcompute/input/traffic/test/diamond-4-hrw-dynamic-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=64000 \
  --islMtuBytes=65535 \
  --islQueueBytes=64000000 \
  --transferLogMode=silent \
  --routingMode=global-hrw-per-flow \
  --ecmpHashSeed=2 \
  --outputDir=/tmp/satcompute-ci-hrw-seed2-a"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-hrw-dynamic \
  --simulationDuration=7 \
  --transferTrace=contrib/satcompute/input/traffic/test/diamond-4-hrw-dynamic-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=64000 \
  --islMtuBytes=65535 \
  --islQueueBytes=64000000 \
  --transferLogMode=silent \
  --routingMode=global-hrw-per-flow \
  --ecmpHashSeed=2 \
  --outputDir=/tmp/satcompute-ci-hrw-seed2-b"
python3 contrib/satcompute/tools/validation/check-ecmp-output.py \
  --hrw-first=/tmp/satcompute-ci-hrw-seed1-a \
  --hrw-second=/tmp/satcompute-ci-hrw-seed1-b \
  --hrw-seed-two-first=/tmp/satcompute-ci-hrw-seed2-a \
  --hrw-seed-two-second=/tmp/satcompute-ci-hrw-seed2-b

announce "size-aware HRW lifecycle"
./waf --run-no-build "satcompute \
  --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-static \
  --simulationDuration=2 \
  --transferTrace=contrib/satcompute/input/traffic/test/size-aware-static-transfers.json \
  --transferChunkMode=fixed \
  --transferPayloadBytes=64000 \
  --islMtuBytes=65535 \
  --islQueueBytes=64000000 \
  --transferLogMode=silent \
  --routingMode=global-hrw-per-flow \
  --ecmpHashSeed=1 \
  --outputDir=/tmp/satcompute-ci-size-static-hrw"
for suffix in a b; do
  ./waf --run-no-build "satcompute \
    --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-static \
    --simulationDuration=2 \
    --transferTrace=contrib/satcompute/input/traffic/test/size-aware-static-transfers.json \
    --transferChunkMode=fixed \
    --transferPayloadBytes=64000 \
    --islMtuBytes=65535 \
    --islQueueBytes=64000000 \
    --transferLogMode=silent \
    --routingMode=global-size-aware-hrw \
    --ecmpHashSeed=1 \
    --outputDir=/tmp/satcompute-ci-size-static-$suffix"
  ./waf --run-no-build "satcompute \
    --topologyDir=contrib/satcompute/input/topology/tests/diamond-4-hrw-dynamic \
    --simulationDuration=7 \
    --transferTrace=contrib/satcompute/input/traffic/test/size-aware-dynamic-transfers.json \
    --transferChunkMode=fixed \
    --transferPayloadBytes=64000 \
    --islMtuBytes=65535 \
    --islQueueBytes=64000000 \
    --transferLogMode=silent \
    --routingMode=global-size-aware-hrw \
    --ecmpHashSeed=1 \
    --outputDir=/tmp/satcompute-ci-size-dynamic-$suffix"
done
python3 contrib/satcompute/tools/validation/check-size-aware-output.py \
  --static-hrw=/tmp/satcompute-ci-size-static-hrw \
  --static-first=/tmp/satcompute-ci-size-static-a \
  --static-second=/tmp/satcompute-ci-size-static-b \
  --dynamic-first=/tmp/satcompute-ci-size-dynamic-a \
  --dynamic-second=/tmp/satcompute-ci-size-dynamic-b

announce "completed"
