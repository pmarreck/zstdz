---
purpose: Work-item tracker for zstdz (Zig-enabled fork of Facebook's zstd)
audience: both
maintained_by: agent
---

# PLAN

- [x] Fix flake ISA poisoning: `packages.default` built with native CPU detection → Garnix cache shipped AVX-512 code to Zen 2 consumers → SIGILL on corrupt-input decode path (`HUF_readDTableX1_wksp`). Fixed with `-Dcpu=baseline` in both flake build phases; added `checks.isa-baseline` (objdump denylist: ymm/zmm/mask regs) and a corrupt-input rejection test (`test_corrupt_decompression`, 8 fixtures) wired into `zig build test`. Reported by validate_gui 2026-07-06; completed 2026-07-06 ~09:15 AM EST.
- [ ] Consider an aarch64 analogue of the ISA-baseline check (SVE/SVE2 poisoning is the same class of bug on aarch64-linux Garnix builders).
- [ ] Watch Garnix rebuild of yolo after push; confirm `isa-baseline` and `default` checks pass on Garnix's AVX-512 builders.
