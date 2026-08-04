# Scenario configuration 0.2

`scenario.schema.json` is the parameter reference and contains a human-readable
`description`, unit suffix, type, and constraint for every scenario field.
Scenario instances remain standard JSON and therefore do not contain comments.

The two maintained 66-satellite inputs demonstrate the independent cadences:

- `synthetic-66-fixed.json` refreshes the online network every 20 s while
  exporting audit slices every 1 s;
- `synthetic-66-distance.json` refreshes distance gates and propagation delays
  every 1 s and also exports every 1 s.

All time values are written in seconds. `scenario_config.py` parses decimal
tokens without a binary floating-point round trip and converts them exactly to
signed 64-bit nanoseconds before any event is scheduled.
