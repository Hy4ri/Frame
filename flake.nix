{
  description = "Frame - A minimal image viewer for Linux";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system: let
      pkgs = import nixpkgs {
        inherit system;
        overlays = [ self.overlays.default ];
      };
      rustPlatform = pkgs.makeRustPlatform {
        cargo = pkgs.cargo;
        rustc = pkgs.rustc;
      };
    in {
      packages.default = rustPlatform.buildRustPackage rec {
        pname = "frame";
        version = "2.0.0";

        src = ./.;

        cargoLock = {
          lockFile = ./Cargo.lock;
        };

        nativeBuildInputs = with pkgs; [
          pkg-config
        ];

        buildInputs = with pkgs; [
          fontconfig
          freetype
          libxcb
          libxkbcommon
          wayland
          vulkan-loader
        ];

        meta = with pkgs.lib; {
          description = "A minimal image viewer for Linux with vim keybindings";
          homepage = "https://github.com/Hy4ri/frame";
          license = licenses.mit;
          maintainers = [];
          platforms = platforms.linux;
          mainProgram = "frame";
        };
      };

      devShells.default = pkgs.mkShell {
        nativeBuildInputs = with pkgs; [
          pkg-config
        ];

        buildInputs = with pkgs; [
          fontconfig
          freetype
          libxcb
          libxkbcommon
          wayland
          vulkan-loader
        ];

        shellHook = ''
          export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath (with pkgs; [ libxcb libxkbcommon wayland vulkan-loader fontconfig freetype ])}:$LD_LIBRARY_PATH"
          export LIBRARY_PATH="${pkgs.lib.makeLibraryPath (with pkgs; [ libxcb libxkbcommon wayland vulkan-loader fontconfig freetype ])}:$LIBRARY_PATH"
          echo "Frame Rust/GPUI development environment loaded"
        '';
      };
      })
      // {
      overlays.default = final: prev: {
        frame = self.packages.${prev.stdenv.hostPlatform.system}.default;
      };
      };
}
