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

        # Generate a loaders.cache that includes WebP support
        loadersCache =
          pkgs.runCommand "gdk-pixbuf-loaders-cache" {
            nativeBuildInputs = [pkgs.gdk-pixbuf];
            GDK_PIXBUF_MODULEDIR = "${pkgs.webp-pixbuf-loader}/lib/gdk-pixbuf-2.0/2.10.0/loaders";
          } ''
            mkdir -p $out
            gdk-pixbuf-query-loaders ${pkgs.gdk-pixbuf}/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so \
              ${pkgs.webp-pixbuf-loader}/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so \
              ${pkgs.librsvg}/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so \
              > $out/loaders.cache
          '';

        # Build dependencies for gotk4
        buildInputs = with pkgs; [
          gtk4
          glib
          gobject-introspection
          gdk-pixbuf
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
          version = "0.3.0";
          src = ./.;

          vendorHash = "sha256-XPZ0zkKCc7CxjZpZvD2VaTpktGaBIQ1+oZRK7UpVX6M=";

          inherit buildInputs nativeBuildInputs;

          # CGO is required for gotk4
          env.CGO_ENABLED = "1";

          # Install desktop file and set up WebP loader
          postInstall = ''
            mkdir -p $out/share/applications
            cp ${./frame.desktop} $out/share/applications/frame.desktop
          '';

          # Wrap the binary to set GDK_PIXBUF_MODULE_FILE for WebP support
          preFixup = ''
            gappsWrapperArgs+=(
              --set GDK_PIXBUF_MODULE_FILE "${loadersCache}/loaders.cache"
            )
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
          inherit buildInputs;
          nativeBuildInputs =
            nativeBuildInputs
            ++ (with pkgs; [
              gopls
              gotools
              go-tools
              webp-pixbuf-loader
              librsvg
            ]);

          # Required for gotk4 to find GTK libraries and WebP loader
          shellHook = ''
            export CGO_ENABLED=1
            export GDK_PIXBUF_MODULE_FILE="${loadersCache}/loaders.cache"
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
