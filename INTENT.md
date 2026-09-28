# Intent

zstdz packages upstream Zstandard as a Zig dependency and a reproducible Nix
library. Consumers, including validate, use the upstream C API through the Zig
module. Preserve upstream decoding behavior, error codes, licenses, and easy
merges from `facebook/zstd` development.

Fork additions must have bounded maintenance cost and retain portable CPU
baselines with runtime dispatch. Verify them through upstream compatibility
tests, the fork's corrupt-input and ISA checks, and supported-target builds.

Peter's 2026-09-28 assignment adds a feasibility study and, if inexpensive, an
optional in-stream corruption-location prototype for validate, as Peter
selected in this conversation. Report detection ranges
honestly, distinguish detection from the damaged byte, measure unused overhead,
and leave validate's implementation to its own agent. See
[the telemetry study](doc/error_telemetry.md) for scope and evidence.
