# TransferTrace 0.1

`transfer-trace.schema.json` is the field reference for direct network-transfer
workloads. Every property has a description, unit, type, and representable
range. The C++ loader additionally checks constraints that JSON Schema cannot
express locally: transfer IDs are unique, source and destination differ and
exist in the active constellation, and every arrival precedes simulation stop.

Unlike user-facing platform intervals in `scenario.schema.json`, the preserved
TransferTrace 0.1 contract writes `arrival_time_ns` as integer nanoseconds. This
keeps existing SatCompute workload files byte-for-byte compatible and prevents
loss of sub-second event precision.

Input array order is not meaningful. The loader sorts transfers by
`transfer_id`, resolves stable service IPv4 addresses, and assigns destination
port 9000. For each source satellite, source ports start at 10000 in canonical
transfer-ID order. Consequently, the same constellation, trace, and hash seed
always produce the same UDP five-tuples.

The trace declares application bytes, not packets. In `fixed` chunk mode,
`workloads.transfer_payload_bytes` is the payload size except for the exact
remainder in the final packet. In `size-aware` mode, the preserved policy is:

- transfers no larger than 1 MiB use 1024-byte payloads;
- transfers larger than 1 MiB and no larger than 64 MiB use 8192 bytes;
- transfers larger than 64 MiB use 64000 bytes.

Packet count, final-packet payload, ports, addresses, transmission spacing, and
routing decisions are derived runtime state and therefore are not accepted as
trace fields.
