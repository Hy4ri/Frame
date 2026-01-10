{
  description = "Frame - A minimal image viewer for Linux";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
  }:
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = nixpkgs.legacyPackages.${system};

        # Build dependencies for gotk4
        buildInputs = with pkgs; [
          gtk4
          glib
          gobject-introspection
          gdk-pixbuf
          webp-pixbuf-loader # WebP image format support
          librsvg # SVG support
          graphene
          cairo
          pango
          harfbuzz
        ];

        nativeBuildInputs = with pkgs; [
          go
          pkg-config
          gobject-introspection
          wrapGAppsHook4
        ];
      in {
        packages.default = pkgs.buildGoModule {
          pname = "frame";
          version = "0.2.0";
          src = ./.;

          vendorHash = "sha256-XPZ0zkKCc7CxjZpZvD2VaTpktGaBIQ1+oZRK7UpVX6M=";

          inherit buildInputs nativeBuildInputs;

          # CGO is required for gotk4
          env.CGO_ENABLED = "1";

          # Install desktop file
          postInstall = ''
            mkdir -p $out/share/applications
            cp ${./frame.desktop} $out/share/applications/frame.desktop
          '';

          # wrapGAppsHook4 should handle GDK_PIXBUF_MODULE_FILE automatically
          # when webp-pixbuf-loader is in buildInputs

          meta = with pkgs.lib; {
            description = "A minimal image viewer for Linux with vim keybindings";
            homepage = "https://github.com/Hy4ri/frame";
            license = licenses.mit;
            maintainers = [];
            platforms = platforms.linux;
          };
        };

        devShells.default = pkgs.mkShell {
          inherit buildInputs;
          nativeBuildInputs =
            nativeBuildInputs
            ++ (with pkgs; [
              gopls
              gotools
              go-tools
            ]);

          # Required for gotk4 to find GTK libraries
          shellHook = ''
            export CGO_ENABLED=1
            echo "Frame development environment loaded"
            echo "Run 'go build' to compile, or 'go run .' to run"
          '';
        };
      }
    )
    // {
      # Overlay for NixOS integration
      overlays.default = final: prev: {
        frame = self.packages.${prev.stdenv.hostPlatform.system}.default;
      };
    };
}
