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
            zig build -Doptimize=ReleaseFast
          '';

          installPhase = ''
            mkdir -p $out/lib $out/include
            cp zig-out/lib/libzstd.a $out/lib/ 2>/dev/null || true
            cp zig-out/include/*.h $out/include/ 2>/dev/null || true
          '';
        };

        checks.default = pkgs.stdenv.mkDerivation {
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
            zig build build_tests -Doptimize=ReleaseFast --cache-dir $ZIG_LOCAL_CACHE_DIR
            ${pkgs.lib.optionalString pkgs.stdenv.isLinux ''
            DL="$(cat ${pkgs.stdenv.cc}/nix-support/dynamic-linker)"
            for f in $(find $ZIG_LOCAL_CACHE_DIR -type f -perm -u+x -name 'test_*'); do
              patchelf --set-interpreter "$DL" "$f" 2>/dev/null || true
            done
            ''}
            zig build test -Doptimize=ReleaseFast --cache-dir $ZIG_LOCAL_CACHE_DIR
          '';

          installPhase = ''
            mkdir -p $out
            echo "tests passed" > $out/result
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
