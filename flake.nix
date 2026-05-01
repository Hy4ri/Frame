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

        # Fyne requires OpenGL and X11/Wayland headers
        buildInputs = with pkgs; [
          libGL
          mesa
          libx11
          libxcursor
          libxrandr
          libxinerama
          libxi
          libxxf86vm
          wayland
          libxkbcommon
        ];

        nativeBuildInputs = with pkgs; [
          go_1_26
          pkg-config
        ];
      in {
        packages.default = (pkgs.buildGoModule.override {go = pkgs.go_1_26;}) {
          pname = "frame";
          version = "0.7.0";
          src = ./.;

          vendorHash = "sha256-qtwMo29HzrB5y4oxXK+dUKioqQGk0+4BBuLpNIg3pZM=";

          inherit buildInputs nativeBuildInputs;

          env.CGO_ENABLED = "1";

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
          inherit buildInputs;
          nativeBuildInputs =
            nativeBuildInputs
            ++ (with pkgs; [
              gopls
              gotools
              go-tools
            ]);

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
