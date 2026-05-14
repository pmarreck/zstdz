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

          nativeBuildInputs = [ zig ];

          dontUseCmakeConfigure = true;
          dontUseZigBuild = true;

          buildPhase = ''
            export ZIG_GLOBAL_CACHE_DIR=$(mktemp -d)
            zig build test -Doptimize=ReleaseFast
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
