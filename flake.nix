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
          # bakes the *builder's* ISA into a cache-shared artifact — the
          # Garnix builder's AVX-512 SIGILLed a Zen 2 consumer the moment a
          # corrupt frame routed into HUF table parsing (2026-07-06). The
          # hand-written BMI2 asm (huf_decompress_amd64.S) is runtime-
          # dispatched and allowed; compiler-emitted AVX/AVX-512
          # (ymm/zmm/mask registers) is not.
          isa-baseline = pkgs.runCommand "zstdz-isa-baseline-check" {
            nativeBuildInputs = [ pkgs.binutils ];
          } ''
            objdump -d ${self.packages.${system}.default}/lib/libzstd.a > disassembly.txt
            if grep -nE '%(ymm|zmm|k[0-7])' disassembly.txt > violations.txt; then
              echo "FAIL: libzstd.a contains instructions beyond the baseline x86-64 ISA:" >&2
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
