# Hypatia orbit bootstrap

This directory provides SatCompute's narrow, reproducible entry point to the
orbit-generation code in Hypatia. It is intentionally limited to:

- generating TLEs;
- reading the generated TLEs; and
- propagating satellite positions at requested times.

It does not import the `satgen` package, run Hypatia's ns-3 model, create ground
stations, or use Hypatia routing and forwarding logic. `hypatia_adapter.py`
loads only the frozen source files named by this interface.

## Frozen environment

The repository root pins Python 3.10.12 in `.python-version`, uv 0.11.25 in
`pyproject.toml`, and all Python dependencies in `uv.lock`. The Hypatia
repository and full commit SHA are recorded in `upstream.json`; the checkout is
created under the ignored `.external/hypatia/` directory.

Set up the environment and checkout from the repository root:

```bash
uv sync --locked
uv run --locked python contrib/satcompute/tools/hypatia/bootstrap.py
```

Running the bootstrap command again is safe. It verifies the origin, refuses a
dirty checkout, and restores the detached checkout to the frozen commit.

The `.venv` directory is created and managed by uv. Activating it with
`source .venv/bin/activate` is optional; reproducible commands and CI use
`uv run --locked`.

## Verification

Run the deterministic six-satellite smoke and its standard-library tests:

```bash
uv run --locked python \
  contrib/satcompute/tools/hypatia/smoke_positions.py --json

uv run --locked python -m unittest discover \
  -s contrib/satcompute/tools/hypatia/tests \
  -p 'test_*.py' -v
```

The smoke generates and reads real Hypatia TLEs, then samples all satellites at
0 and 60 seconds. It uses Hypatia's near-circular eccentricity value
`0.0000001`; a mathematically exact zero is not propagatable by PyEphem. The
reported aggregate SHA-256 makes repeated output directly comparable.

## Synthetic 66 constellation contract

PR2 freezes an Iridium-scale synthetic Walker Star preset rather than claiming
to reproduce the real Iridium constellation:

```text
pattern       : walker-star
planes        : 6
slots/plane   : 11
altitude      : 780 km
inclination   : 86.4 degrees
RAAN          : 0, 30, 60, 90, 120, 150 degrees
phase scheme  : alternating half-slot
seam          : disabled
```

Generate its TLE set and resolved provenance manifest:

```bash
uv run --locked python \
  contrib/satcompute/tools/hypatia/resolve_constellation.py \
  --output-dir /tmp/satcompute-synthetic-66
```

The command writes only `tles.txt` and `resolved-manifest.json`. The manifest
records the physical inputs, WGS72-derived mean motion, frozen Hypatia and tool
versions, and deterministic TLE and position hashes. Walker Delta delegates to
the frozen Hypatia 360-degree RAAN generator; Walker Star uses the local
180-degree adaptation in `walker_tles.py` because frozen Hypatia does not
provide that pattern.

This stage does not generate candidate ISLs, apply the seam rule, export
SatCompute `nodes_*.json` or `topology_*.json`, or run a 1000-second trajectory.

Do not commit `.external/`, `.venv/`, generated TLEs, or smoke output.
