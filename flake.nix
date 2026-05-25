{
  description = "Frame - A minimal image viewer for Linux";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system: let
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      packages.default = pkgs.stdenv.mkDerivation {
        pname = "frame";
        version = "0.9.0";
        src = ./.;

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
        ];

        # Install desktop file
        postInstall = ''
          mkdir -p $out/share/applications
          cp ${./frame.desktop} $out/share/applications/frame.desktop
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
      # Overlay for NixOS integration
      overlays.default = final: prev: {
        frame = self.packages.${prev.stdenv.hostPlatform.system}.default;
      };
    };
}
