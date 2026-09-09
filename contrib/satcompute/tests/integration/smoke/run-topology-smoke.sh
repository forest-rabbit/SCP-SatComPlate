#!/usr/bin/env bash

set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$repository_root"

smoke_output="$(mktemp -d /tmp/satcompute-topology-smoke.XXXXXX)"
trap 'rm -rf "$smoke_output"' EXIT

constellation="contrib/satcompute/tests/fixtures/constellation/diamond-4.csv"
arguments="--simulationDuration=2.5 --constellationConfig=$constellation \
--maxIslDistance=1 --delayMode=distance --fixedDelay=0 \
--networkUpdateInterval=2 --islBandwidthBps=100000000 \
--topologyOnly=1 --topologySliceInterval=1 --includeFinalTopologyState=1"

first="$(./ns3 run --no-build \
  "satcompute $arguments --outputDir=$smoke_output/first")"
second="$(./ns3 run --no-build \
  "satcompute $arguments --outputDir=$smoke_output/second")"
for result in "$first" "$second"; do
  if [[ "$result" != *'"status":"topology-only"'* ||
        "$result" != *'"slice_count":4'* ]]; then
    echo "topology-only smoke failed: $result" >&2
    exit 1
  fi
done

python3 - "$smoke_output/first/topology" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
expected = {
    "nodes_0s.json",
    "nodes_1s.json",
    "nodes_2s.json",
    "nodes_2.5s.json",
    "links_0s.json",
    "links_1s.json",
    "links_2s.json",
    "links_2.5s.json",
}
actual = {path.name for path in root.iterdir() if path.is_file()}
if actual != expected:
    raise SystemExit(f"topology-only inventory differs: {sorted(actual)}")


def read(name: str):
    with (root / name).open(encoding="utf-8") as source:
        return json.load(source)


nodes_zero = read("nodes_0s.json")
nodes_one = read("nodes_1s.json")
if set(nodes_zero) != {"simulation_time_ns", "nodes"}:
    raise SystemExit("node slice root is not minimal")
if len(nodes_zero["nodes"]) != 4:
    raise SystemExit("node slice count differs")
if set(nodes_zero["nodes"][0]) != {"node_id", "node_type", "x", "y", "z"}:
    raise SystemExit("node slice fields differ")
if nodes_zero["nodes"][0]["x"] == nodes_one["nodes"][0]["x"]:
    raise SystemExit("native orbit coordinates did not evolve")

links = read("links_2.5s.json")
if set(links) != {"simulation_time_ns", "links"}:
    raise SystemExit("link slice root is not minimal")
if len(links["links"]) != 4:
    raise SystemExit("inactive fixed candidates were omitted")
expected_link_fields = {
    "node1_id",
    "node2_id",
    "type",
    "active",
    "distance_m",
    "delay_ns",
    "link_bandwidth_bps",
}
for link in links["links"]:
    if set(link) != expected_link_fields:
        raise SystemExit("link slice fields differ")
    if link["active"]:
        raise SystemExit("one-meter gate unexpectedly activated a link")
    if link["distance_m"] <= 1 or link["delay_ns"] <= 0:
        raise SystemExit("link distance or propagation delay differs")
    if link["link_bandwidth_bps"] != 100_000_000:
        raise SystemExit("link bandwidth differs")
PY

diff -ru "$smoke_output/first/topology" "$smoke_output/second/topology"

# Omit fixedDelay to check the platform default reaches exported link state.
./ns3 run --no-build \
  "satcompute --simulationDuration=1 --constellationConfig=$constellation \
--topologyOnly=1 --outputDir=$smoke_output/fixed-default"

python3 - "$smoke_output/fixed-default/topology" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
for filename in ("links_0s.json", "links_1s.json"):
    links = json.loads((root / filename).read_text())["links"]
    if not links or any(link["delay_ns"] != 1_000_000 for link in links):
        raise SystemExit("default fixed delay is not 1 ms in exported link state")
PY

echo "SatCompute native topology-only smoke passed."
