{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  name = "lean-wlroots-dev";

  buildInputs = with pkgs; [
    # --- Lean toolchain ---
    elan
    gmp
    libuv

    # --- C toolchain ---
    clang
    libcxx
    libcxxrt
    lld
    pkg-config
    gnumake

    # --- Wayland stack ---
    wlroots
    wayland
    wayland-protocols
    libxkbcommon
    libinput
    pixman
    mesa
    libdrm
    libudev-zero  # or systemd if needed

    # --- Debug / optional but useful ---
    gdb
    strace
    lldb
  ];

  # WLR_USE_UNSTABLE = "1";

  shellHook = ''
    export LEAN_CC=clang
    export LEAN_AR=ar

    export CC=clang
    export CXX=clang++
    export PKG_CONFIG_PATH=${pkgs.wlroots}/lib/pkgconfig:$PKG_CONFIG_PATH
  
    echo "PKG_CONFIG_PATH=$PKG_CONFIG_PATH"

    pkg-config --modversion wlroots-0.19 || echo "wlroots not visible" 
  '';
}
