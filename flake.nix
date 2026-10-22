{
  description = "NOCTIS - a retro gothic shooter written in plain C with no external libraries";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (system: pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "noctis";
          version = "1.0.0";
          src = self;
          # No buildInputs: the game talks to X11 and PulseAudio over their
          # wire protocols, so it only needs libc and libm.
          makeFlags = [ "PREFIX=$(out)" ];
          enableParallelBuilding = true;
          meta = {
            description = "Retro gothic raycasting shooter in plain C";
            mainProgram = "noctis";
            platforms = systems;
          };
        };
      });

      apps = forAllSystems (system: pkgs: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/noctis";
        };
      });

      devShells = forAllSystems (system: pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            gcc
            clang
            clang-tools   # clangd, clang-format
            gnumake
            gdb
            valgrind
            bear          # compile_commands.json: `bear -- make`
            imagemagick   # convert --shot PPM screenshots to PNG
          ];
          shellHook = ''
            echo "NOCTIS dev shell - 'make run' to play, 'make shots' for headless screenshots"
          '';
        };
      });
    };
}
