---
purpose: Environment/tooling lessons learned while working on zstdz
audience: agent
maintained_by: agent
---

# LEARNINGS

## Nix + Zig native CPU detection = binary-cache ISA poisoning (2026-07-06)

`b.standardTargetOptions(.{})` defaults to **native CPU feature detection**. Inside a Nix
derivation this is an impurity Nix cannot see: the same drv hash yields different machine
code depending on which builder ran it. Garnix's AVX-512-capable builder produced a
`libzstd.a` containing AVX-512VL/BW instructions; Nix substituted that "identical" store
path onto a Zen 2 (no AVX-512) machine, and any consumer SIGILLed — but *only* on corrupt
input, because the AVX-512 hotspot was `HUF_readDTableX1_wksp` (Huffman table parsing),
which tiny raw-literals valid frames never reach. Diagnosis chain that worked:
`nix path-info --sigs <path>` (proved cache.garnix.io substitution) → `objdump -d` + grep
for `%(ymm|zmm|k[0-7])` (proved AVX-512) → awk mapping offending instructions to symbols
(explained the corrupt-only trigger). Fix: `-Dcpu=baseline` in every flake `zig build`,
guarded by a `checks.isa-baseline` disassembly denylist that Garnix itself runs (its
builders are exactly where a regression would reintroduce AVX-512 — the check is
adversarially placed). Runtime-dispatched hand-written asm (BMI2 in
`huf_decompress_amd64.S`) is safe and stays allowed.

**This bug class exists in ANY sibling Zig project whose flake package is shared via the
Garnix cache without an explicit `-Dcpu=`.**
