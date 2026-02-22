{
  description = "Lean4 + wlroots compositor skeleton";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in {
        devShells.default = pkgs.mkShell {
          name = "lean-wlroots-dev";

          packages = with pkgs; [
            elan
            gmp
            libuv
            clang
            lld
            libcxx
            libcxxrt
            pkg-config
            gnumake
            wlroots
            wayland
            wayland-protocols
            libxkbcommon
            libinput
            pixman
            mesa
            libdrm
            libudev-zero
            seatd
            wofi
            foot
          ];

          shellHook = ''
            export LEAN_CC=clang
            export CC=clang
            export CXX=clang++
            export PKG_CONFIG_PATH=${pkgs.wlroots}/lib/pkgconfig:$PKG_CONFIG_PATH
            echo "wlroots: $(pkg-config --modversion wlroots 2>/dev/null || pkg-config --modversion wlroots-0.19 2>/dev/null || echo missing)"
          '';
        };
      });
}
