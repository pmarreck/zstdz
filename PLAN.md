---
purpose: Work-item tracker for zstdz (Zig-enabled fork of Facebook's zstd)
audience: both
maintained_by: agent
---

# PLAN

- [x] Merge 77 upstream commits through facebook/zstd dev 01b7154f, preserving Zig packaging and baseline-CPU safeguards (done 2026-09-24 16:38 EDT; enclosing merge commit).
- [x] Refresh flake inputs to 2026-09-24, drop unsupported Intel macOS, and pass Linux package, smoke, corrupt-input, ISA, upstream CLI, seeded compression/streaming, legacy, and ASan/UBSan checks (done 2026-09-24 16:38 EDT; enclosing merge commit).

- [x] Refine `checks.isa-baseline` to permit platform-agnostic SIMD via runtime dispatch (Peter's directive): symbol allowlist for zstd's DYNAMIC_BMI2 family (`*_bmi2`, `*_fast_asm_loop`, `*_fast_c_loop`, HUF `_internal_fast` wrappers — all target-attributed + cpuid-gated), hard denial of vector regs AND post-baseline scalar mnemonics everywhere else (tzcnt exempt: rep-bsf compatible encoding). Differentially validated: baseline+allowlist=0, baseline−allowlist=774, poisoned lib=6405 violations (up from 3293 under the vector-only check). Completed 2026-07-06 ~11:45 AM EST.
- [x] Fix flake ISA poisoning: `packages.default` built with native CPU detection → Garnix cache shipped AVX-512 code to Zen 2 consumers → SIGILL on corrupt-input decode path (`HUF_readDTableX1_wksp`). Fixed with `-Dcpu=baseline` in both flake build phases; added `checks.isa-baseline` (objdump denylist: ymm/zmm/mask regs) and a corrupt-input rejection test (`test_corrupt_decompression`, 8 fixtures) wired into `zig build test`. Reported by validate_gui 2026-07-06; completed 2026-07-06 ~09:15 AM EST.
- [ ] Consider an aarch64 analogue of the ISA-baseline check (SVE/SVE2 poisoning is the same class of bug on aarch64-linux Garnix builders).
- [x] Watch Garnix rebuild of yolo; confirmed `isa-baseline` + `default` checks GREEN on Garnix's AVX-512 builders for both 0a478bb and 953650e6. Caveat: the "All Garnix checks" *aggregate* hangs in_progress indefinitely (~3h observed) despite all sub-checks green — wind-down symptom; trust individual check contexts, never gate on the aggregate. (Garnix EOL 2026-07-15, Shopify acquisition.) Completed 2026-07-06 ~12:10 PM EST.
- [x] Replace the Garnix badge with Mechatron Prime and commit the Linux package, smoke-test, and ISA-check target manifest; existing push webhook verified active (done 2026-09-24 16:38 EDT; enclosing merge commit).
