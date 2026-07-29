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

Do not commit `.external/`, `.venv/`, generated TLEs, or smoke output.
