{
  description = "zstdz — Zig-enabled fork of Facebook's zstd compression library";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    zig-overlay = {
      url = "github:mitchellh/zig-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, flake-utils, zig-overlay }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        zig = zig-overlay.packages.${system}."0.16.0";
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "zstdz";
          version = "1.6.0";
          src = ./.;

          nativeBuildInputs = [ zig ];

          dontUseCmakeConfigure = true;
          dontUseZigBuild = true;

          buildPhase = ''
            export ZIG_GLOBAL_CACHE_DIR=$(mktemp -d)
            # -Dcpu=baseline: a Nix-built artifact is shared via binary caches
            # (Garnix), so it must not inherit the builder's native ISA —
            # native detection here once shipped AVX-512 to a Zen 2 machine
            # (SIGILL). Hot paths keep their speed via zstd's runtime BMI2
            # dispatch. Guarded by checks.isa-baseline.
            zig build -Doptimize=ReleaseFast -Dcpu=baseline
          '';

          installPhase = ''
            mkdir -p $out/lib $out/include
            cp zig-out/lib/libzstd.a $out/lib/
            cp zig-out/include/*.h $out/include/
          '';
        };

        checks = {
          default = pkgs.stdenv.mkDerivation {
            pname = "zstdz-check";
            version = "1.6.0";
            src = ./.;

            nativeBuildInputs = [ zig ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [ pkgs.patchelf ];

            dontUseCmakeConfigure = true;
            dontUseZigBuild = true;

            buildPhase = ''
              export ZIG_CACHE=$(mktemp -d)
              export ZIG_GLOBAL_CACHE_DIR=$ZIG_CACHE
              export ZIG_LOCAL_CACHE_DIR=$ZIG_CACHE/local
              mkdir -p $ZIG_LOCAL_CACHE_DIR

              # First, build everything without running tests so the test_*
              # artifacts land in .zig-cache. Then patchelf them so the FHS
              # interpreter path doesn't break exec inside the Nix sandbox.
              # Finally, re-run `zig build test` which reuses the cached and
              # now-patched binaries.
              zig build build_tests -Doptimize=ReleaseFast -Dcpu=baseline --cache-dir $ZIG_LOCAL_CACHE_DIR
              ${pkgs.lib.optionalString pkgs.stdenv.isLinux ''
              DL="$(cat ${pkgs.stdenv.cc}/nix-support/dynamic-linker)"
              for f in $(find $ZIG_LOCAL_CACHE_DIR -type f -perm -u+x -name 'test_*'); do
                patchelf --set-interpreter "$DL" "$f" 2>/dev/null || true
              done
              ''}
              zig build test -Doptimize=ReleaseFast -Dcpu=baseline --cache-dir $ZIG_LOCAL_CACHE_DIR
            '';

            installPhase = ''
              mkdir -p $out
              echo "tests passed" > $out/result
            '';
          };
        } // pkgs.lib.optionalAttrs (pkgs.stdenv.isLinux && pkgs.stdenv.hostPlatform.isx86_64) {
          # MFIC control: the shipped static lib must execute on baseline
          # x86-64. Zig's default native-CPU detection inside a Nix build
          # bakes the *builder's* ISA into a cache-shared artifact — a
          # builder's AVX-512 SIGILLed a Zen 2 consumer the moment a corrupt
          # frame routed into HUF table parsing (2026-07-06).
          #
          # Platform-agnostic SIMD/ISA-extension semantics ARE permitted —
          # but only via runtime dispatch: symbols matching the allowlist
          # below (zstd's DYNAMIC_BMI2 family: target-attributed C compiled
          # for lzcnt/bmi/bmi2, plus cpuid-guarded hand-written asm) may use
          # any extension, because they are only reachable behind a runtime
          # CPU-feature check. Everywhere else the artifact must be pure
          # baseline: no vector extensions (ymm/zmm/opmask registers) and no
          # post-baseline scalar mnemonics. tzcnt is globally permitted: its
          # rep-bsf encoding executes as bsf on pre-BMI1 CPUs and compilers
          # only emit it where that fallback is correct. Extending the
          # allowlist requires the new symbol to be provably runtime-
          # dispatched — review its call sites before blessing it.
          isa-baseline = pkgs.runCommand "zstdz-isa-baseline-check" {
            nativeBuildInputs = [ pkgs.binutils ];
          } ''
            objdump -d ${self.packages.${system}.default}/lib/libzstd.a > disassembly.txt
            awk -v ALLOW='(_bmi2|_fast_asm_loop|_fast_c_loop)$|^HUF_decompress4X[12]_usingDTable_internal_fast$' '
              /^[0-9a-f]+ <.*>:$/ { fn = substr($2, 2, length($2)-3); next }
              {
                n = split($0, parts, "\t")
                if (n < 3) next
                if (ALLOW != "" && fn ~ ALLOW) next
                split(parts[3], w, " ")
                m = w[1]
                if (parts[3] ~ /%(ymm|zmm|k[0-7])/ ||
                    m ~ /^(shrx|shlx|sarx|andn|bzhi|pdep|pext|mulx|rorx|lzcnt|popcnt|movbe|crc32|aesenc|aesenclast|aesdec|aesdeclast|aesimc|aeskeygenassist|pclmulqdq)$/) {
                  print fn ": " parts[3]
                }
              }
            ' disassembly.txt > violations.txt
            if [ -s violations.txt ]; then
              echo "FAIL: non-baseline instructions outside runtime-dispatched symbols:" >&2
              head -20 violations.txt >&2
              echo "(total: $(wc -l < violations.txt) offending lines)" >&2
              exit 1
            fi
            echo "ISA baseline check passed" > $out
          '';
        };

        devShells.default = pkgs.mkShell {
          buildInputs = [
            zig
          ];
        };
      }
    );
}
