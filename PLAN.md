---
purpose: Work-item tracker for zstdz (Zig-enabled fork of Facebook's zstd)
audience: both
maintained_by: agent
---

# PLAN

- [x] Refine `checks.isa-baseline` to permit platform-agnostic SIMD via runtime dispatch (Peter's directive): symbol allowlist for zstd's DYNAMIC_BMI2 family (`*_bmi2`, `*_fast_asm_loop`, `*_fast_c_loop`, HUF `_internal_fast` wrappers — all target-attributed + cpuid-gated), hard denial of vector regs AND post-baseline scalar mnemonics everywhere else (tzcnt exempt: rep-bsf compatible encoding). Differentially validated: baseline+allowlist=0, baseline−allowlist=774, poisoned lib=6405 violations (up from 3293 under the vector-only check). Completed 2026-07-06 ~11:45 AM EST.
- [x] Fix flake ISA poisoning: `packages.default` built with native CPU detection → Garnix cache shipped AVX-512 code to Zen 2 consumers → SIGILL on corrupt-input decode path (`HUF_readDTableX1_wksp`). Fixed with `-Dcpu=baseline` in both flake build phases; added `checks.isa-baseline` (objdump denylist: ymm/zmm/mask regs) and a corrupt-input rejection test (`test_corrupt_decompression`, 8 fixtures) wired into `zig build test`. Reported by validate_gui 2026-07-06; completed 2026-07-06 ~09:15 AM EST.
- [ ] Consider an aarch64 analogue of the ISA-baseline check (SVE/SVE2 poisoning is the same class of bug on aarch64-linux Garnix builders).
- [ ] Watch Garnix rebuild of yolo after push; confirm `isa-baseline` and `default` checks pass on Garnix's AVX-512 builders. **Time-boxed: Garnix shuts down 2026-07-15 (Shopify acquisition); Peter's local build-system replacement takes over CI duty after that.** The check itself stays valid under any shared-cache builder.
- [ ] Post-Garnix (after 2026-07-15): remove/replace any Garnix badge in README.md and re-point CI to the local build system once it lands.
