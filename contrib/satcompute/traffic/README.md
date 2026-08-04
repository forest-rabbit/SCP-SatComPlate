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

## Runtime lifecycle

`NetworkTransferEngine` registers every stable five-tuple before the first
arrival. One receiver socket is shared by all transfers terminating at the
same satellite, while every transfer owns one sender application. The first
packet is submitted exactly at `arrival_time_ns`; subsequent packets are paced
by the current first-hop serialization time. Capacity-aware routing instead
uses its admitted complete-path bottleneck rate.

Size-aware reservations begin immediately before first send and end after the
sender submits its final packet. Capacity-aware paths remain reserved until
the receiver obtains the declared application bytes. If no complete path has
residual capacity, a flow sends no bytes and waits; completion or a route
update retries pending flows in deterministic order. A topology update pauses
an active sender whose admitted path became invalid, releases the entire old
path, and resumes only after complete-path re-admission.

The transport remains UDP-compatible with the legacy platform and does not
invent retransmissions. A future fault model may therefore produce an
incomplete transfer when it disables a link carrying an in-flight packet; that
outcome is reported by completion policy and diagnostics rather than hidden by
this engine.
