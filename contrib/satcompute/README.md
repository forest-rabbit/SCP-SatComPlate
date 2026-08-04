# SatCompute on ns-3.48

This contrib module is the main SCP-SatComPlate implementation. It is built as
a normal ns-3 library plus a module-owned `satcompute` executable, so neither
the global ns-3 examples switch nor the upstream test suite is required.

From the repository root:

```bash
./ns3 configure --enable-modules=satcompute
./ns3 build
python3 -m unittest discover \
  -s contrib/satcompute/tests/unit -p 'test_*.py' -v
contrib/satcompute/tests/integration/smoke/run-all.sh
```

The current scenario contract is `config/scenario.schema.json`. Its
`description` entries are the parameter reference; maintained configurations
are under `input/examples/`.
