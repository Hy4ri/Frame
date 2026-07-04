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
        version = "1.3.0";

        src = pkgs.fetchzip {
          url = "https://github.com/Hy4ri/Frame/releases/download/v${version}/frame-linux-x86_64.tar.gz";
          hash = "sha256-AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
        };

        nativeBuildInputs = with pkgs; [
          autoPatchelfHook
        ];

        buildInputs = with pkgs; [
          sdl3
          sdl3-image
          sdl3-ttf
          libexif
          stdenv.cc.cc.lib
        ];

        dontConfigure = true;
        dontBuild = true;

        installPhase = ''
          mkdir -p $out/bin
          cp frame $out/bin/

          mkdir -p $out/share/applications
          cp frame.desktop $out/share/applications/frame.desktop
        '';

        meta = with pkgs.lib; {
          description = "A minimal image viewer for Linux with vim keybindings";
          homepage = "https://github.com/Hy4ri/frame";
          license = licenses.mit;
          maintainers = [];
          platforms = platforms.linux;
        };
      };

      devShells.default = pkgs.mkShell {
        nativeBuildInputs = with pkgs; [
          meson
          ninja
          pkg-config
        ];

        buildInputs = with pkgs; [
          sdl3
          sdl3-image
          sdl3-ttf
          libexif
          clang-tools
        ];

        shellHook = ''
          echo "Frame C development environment loaded"
          echo "Run 'meson setup build && ninja -C build' to compile"
          echo "Run './build/frame /path/to/image.jpg' to run"
        '';
      };
    })
    // {
      # Overlay providing trimmed SDL3 packages for Frame
      overlays.default = final: prev: {
        # Trimmed SDL3 — only what Frame actually needs (no audio, camera, BT, dialogs, etc.)
        sdl3-trimmed = (prev.sdl3.override {
          # Audio — Frame is an image viewer, no audio needed
          alsaSupport = false;
          pipewireSupport = false;
          pulseaudioSupport = false;
          jackSupport = false;
          sndioSupport = false;
          # Input methods — not needed
          ibusSupport = false;
          # System tray — not needed
          traySupport = false;
          # USB HID — keyboard/mouse handled by SDL's own input layer
          libusbSupport = false;
          # Vulkan — OpenGL is sufficient for 2D image rendering
          vulkanSupport = false;
        }).overrideAttrs (old: {
          # Tests expect audio/input subsystems that we disabled — skip them
          doCheck = false;
          # Remove zenity from postPatch — Frame never shows file dialogs or message boxes.
          # Zenity pulls in GTK4 → gst-plugins-bad → ~918 MiB of unnecessary closure.
          postPatch = let
            lib = final.lib;
            lines = lib.strings.splitString "\n" old.postPatch;
            filtered = builtins.filter (
              line: !lib.strings.hasInfix "zenity" line
            ) lines;
          in lib.strings.concatStringsSep "\n" filtered;
        });

        # Use the trimmed SDL3 for sdl3-image and sdl3-ttf too
        sdl3-image-trimmed = prev.sdl3-image.override {
          sdl3 = final.sdl3-trimmed;
        };

        sdl3-ttf-trimmed = prev.sdl3-ttf.override {
          sdl3 = final.sdl3-trimmed;
        };

        frame = self.packages.${prev.stdenv.hostPlatform.system}.default;
      };
    };
}
