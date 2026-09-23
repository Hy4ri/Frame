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
    in {
      packages.default = pkgs.stdenv.mkDerivation rec {
        pname = "frame";
        version = "2.0.0";

        src = pkgs.fetchzip {
          url = "https://github.com/Hy4ri/Frame/releases/download/v${version}/frame-linux-x86_64.tar.gz";
          hash = "sha256-BTw62B32mvvPA6vPIWVVAPAPp8pCwbMcJiTr1ta9ctE=";
          stripRoot = false;
        };

        nativeBuildInputs = with pkgs; [
          autoPatchelfHook
          makeWrapper
        ];

        buildInputs = with pkgs; [
          libxcb
          libxkbcommon
          stdenv.cc.cc.lib
          wayland
        ];

        dontConfigure = true;
        dontBuild = true;

        installPhase = ''
          mkdir -p $out/bin
          makeWrapper $src/frame $out/bin/frame \
            --prefix LD_LIBRARY_PATH : ${pkgs.lib.makeLibraryPath [ pkgs.wayland ]}

          mkdir -p $out/share/applications
          cp frame.desktop $out/share/applications/frame.desktop
        '';

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
