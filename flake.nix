{
  description = "NOCTIS - a retro gothic shooter in plain C on SDL3, built against the Steam Linux Runtime 4 SDK";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system nixpkgs.legacyPackages.${system});

      # Steam Linux Runtime 4 ("steamrt4", Debian 13 based). Hashes come from
      # the SHA256SUMS file published next to the images. To bump: change
      # `version` and copy the new hashes from ${base}/SHA256SUMS.
      steamrt = rec {
        version = "4.0.20261021.265997";
        base = "https://repo.steampowered.com/steamrt4/images/${version}";
        x86_64-linux = {
          sdk = { file = "com.valvesoftware.SteamRuntime.Sdk-amd64,i386-steamrt4-sysroot.tar.gz"; sha256 = "c01a7507cea248c231412f3d70563501899c3e77a8c9f08078a7c7cf30bee6a6"; };
          runtime = { file = "SteamLinuxRuntime_4.tar.xz"; sha256 = "aa9dd1aab2173e65b631faae34b85e2c17fe323d941013154ddd2e1dce8a0910"; };
          triplet = "x86_64-linux-gnu";
          interpreter = "/lib64/ld-linux-x86-64.so.2";
        };
        aarch64-linux = {
          sdk = { file = "com.valvesoftware.SteamRuntime.Sdk-arm64-steamrt4-sysroot.tar.gz"; sha256 = "58e5a73d3f0d39c47bb6f0b194240ee67a004fd89c2f0e996b33f27deb333067"; };
          runtime = { file = "SteamLinuxRuntime_4-arm64.tar.xz"; sha256 = "a6a6a6e915486bc54563acb18228952008c76645db97d3e555a1f690d7455061"; };
          triplet = "aarch64-linux-gnu";
          interpreter = "/lib/ld-linux-aarch64.so.1";
        };
      };

      # The only shared libraries a release binary may depend on. All of them
      # are provided by the Steam Linux Runtime 4 container.
      allowedNeeded = [ "libSDL3.so.0" "libm.so.6" "libc.so.6" ];

      perSystem = system: pkgs:
        let
          rt = steamrt.${system};
          lib = pkgs.lib;

          # The SDK sysroot, unpacked once into the store.
          sdkSysroot = pkgs.runCommand "steamrt4-sdk-sysroot-${steamrt.version}" {
            src = pkgs.fetchurl {
              url = "${steamrt.base}/${rt.sdk.file}";
              name = "steamrt4-sdk-sysroot.tar.gz";
              inherit (rt.sdk) sha256;
            };
          } ''
            mkdir -p $out
            # setuid/setgid bits and ownership cannot exist in the store
            tar -xzf $src -C $out --no-same-owner --no-same-permissions
            chmod -R u+w,ug-s $out
          '';

          # `steamrt4-sdk CMD...` runs CMD inside the SDK with bubblewrap: the
          # SDK's own gcc, headers, pkg-config and libraries, nothing from Nix.
          # Only the directory in $STEAMRT4_SDK_BIND (default: $PWD) is visible,
          # read-write at the same path, and there is no network.
          sdk = pkgs.writeShellApplication {
            name = "steamrt4-sdk";
            runtimeInputs = [ pkgs.bubblewrap pkgs.coreutils ];
            text = ''
              root=${sdkSysroot}
              bind=$(realpath "''${STEAMRT4_SDK_BIND:-$PWD}")
              args=()
              for entry in "$root"/*; do
                name=''${entry##*/}
                case $name in dev|proc|sys|tmp|run|home|root) continue ;; esac
                if [ -L "$entry" ]; then args+=(--symlink "$(readlink "$entry")" "/$name")
                else args+=(--ro-bind "$entry" "/$name"); fi
              done
              env_args=(--setenv PATH /usr/bin:/bin --setenv HOME /tmp --setenv LANG C.UTF-8)
              for v in TERM SOURCE_DATE_EPOCH; do
                if [ -n "''${!v:-}" ]; then env_args+=(--setenv "$v" "''${!v}"); fi
              done
              exec bwrap "''${args[@]}" \
                --dev /dev --proc /proc --tmpfs /tmp --dir /run \
                --bind "$bind" "$bind" --chdir "$(realpath "$PWD")" \
                --unshare-all --die-with-parent --clearenv "''${env_args[@]}" \
                -- "$@"
            '';
          };

          # Steam's own container launcher (pressure-vessel) and the steamrt4
          # platform: the exact runtime Steam starts the game in.
          steamLinuxRuntime = pkgs.runCommand "SteamLinuxRuntime_4-${steamrt.version}" {
            src = pkgs.fetchurl {
              url = "${steamrt.base}/${rt.runtime.file}";
              inherit (rt.runtime) sha256;
            };
          } ''
            mkdir -p $out
            tar -xJf $src -C $out --no-same-owner --no-same-permissions --strip-components=1
            # The game is 64-bit only: set up just the primary architecture
            # (the i386 half would need a 32-bit loader on the host).
            sed -i 's/^\(export PRESSURE_VESSEL_ARCHITECTURES=[^:]*\):.*/\1/' $out/run
            grep -q '^export PRESSURE_VESSEL_ARCHITECTURES=${rt.triplet}$' $out/run
          '';

          # pressure-vessel expects an FHS host (/usr/bin, /etc/ld.so.cache,
          # graphics drivers under /usr). On NixOS we provide one the same way
          # nixpkgs' Steam does; on other distros the FHS env is a thin layer.
          # `steamrt4-run CMD...` runs CMD inside the Steam Linux Runtime 4.
          run = pkgs.buildFHSEnv {
            name = "steamrt4-run";
            targetPkgs = p: with p; [
              bash coreutils getent
              libdrm libGL libglvnd mesa vulkan-loader
              wayland libxkbcommon
              libX11 libXcursor libXext libXfixes libXi libXrandr libXScrnSaver libxcb
              pipewire libpulseaudio alsa-lib
              dbus udev
            ];
            # The FHS env's /usr/bin/ldconfig is a shell wrapper. pressure-vessel
            # then looks for the real ELF next to it as ldconfig.real (the
            # Debian/Ubuntu convention) to learn its architecture.
            extraBuildCommands = ''
              ln -s ${pkgs.glibc.bin}/bin/ldconfig $out/usr/bin/ldconfig.real
            '';
            # pressure-vessel takes locks inside the runtime directory, so it
            # cannot run from the read-only store: copy it once to the cache.
            runScript = pkgs.writeShellScript "steamrt4-run" ''
              set -eu
              cache="''${XDG_CACHE_HOME:-$HOME/.cache}/steamrt4"
              rt="$cache/${baseNameOf steamLinuxRuntime}"
              if [ ! -e "$rt/.complete" ]; then
                echo "steamrt4-run: installing the runtime into $rt" >&2
                rm -rf "$rt.tmp"
                mkdir -p "$cache"
                cp -a --reflink=auto --no-preserve=ownership ${steamLinuxRuntime} "$rt.tmp"
                chmod -R u+w "$rt.tmp"
                rm -rf "$rt"
                mv "$rt.tmp" "$rt"
                touch "$rt/.complete"
              fi
              exec "$rt/run" -- "$@"
            '';
          };

          version = "1.0.0";
          src = lib.fileset.toSource {
            root = ./.;
            fileset = lib.fileset.unions [ ./Makefile ./src ];
          };

          # The Steam release: built entirely with the SDK toolchain, so the
          # binary only links against libraries in the Steam Linux Runtime and
          # contains no Nix store paths. $out is a ready-to-upload depot root.
          release = pkgs.stdenvNoCC.mkDerivation {
            pname = "noctis-steamrt4";
            inherit version;
            inherit src;
            outputs = [ "out" "debug" ];
            nativeBuildInputs = [ sdk ];

            buildPhase = ''
              runHook preBuild
              export STEAMRT4_SDK_BIND=$PWD
              make -j$NIX_BUILD_CORES CC="steamrt4-sdk gcc" PKG_CONFIG="steamrt4-sdk pkg-config" \
                CFLAGS="-O2 -g -ffile-prefix-map=$PWD=."
              runHook postBuild
            '';

            doCheck = true;
            checkPhase = ''
              runHook preCheck
              interp=$(steamrt4-sdk readelf -l noctis | sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p')
              if [ "$interp" != "${rt.interpreter}" ]; then
                echo "unexpected ELF interpreter: $interp"; exit 1
              fi
              for lib in $(steamrt4-sdk readelf -d noctis | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
                case " ${lib.concatStringsSep " " allowedNeeded} " in
                  *" $lib "*) ;;
                  *) echo "unexpected shared library dependency: $lib"; exit 1 ;;
                esac
              done
              if steamrt4-sdk readelf -d noctis | grep -qE '\((RPATH|RUNPATH)\)'; then
                echo "release binary must not have an RPATH"; exit 1
              fi
              if grep -q /nix/store noctis; then
                echo "release binary references the Nix store"; exit 1
              fi
              steamrt4-sdk ./noctis --validate
              steamrt4-sdk ./noctis --shot build/check.ppm --level 1 --frames 10
              runHook postCheck
            '';

            installPhase = ''
              runHook preInstall
              mkdir -p $out $debug/lib/debug
              steamrt4-sdk objcopy --only-keep-debug noctis build/noctis.debug
              steamrt4-sdk strip --strip-debug --strip-unneeded noctis
              steamrt4-sdk objcopy --add-gnu-debuglink=build/noctis.debug noctis
              install -m755 noctis $out/noctis
              install -m644 build/noctis.debug $debug/lib/debug/noctis.debug
              runHook postInstall
            '';

            # Ship exactly what the SDK produced: no patchelf, no Nix strip.
            dontFixup = true;
            allowedReferences = [ ];

            passthru = { inherit sdk sdkSysroot steamLinuxRuntime run; };
            meta = {
              description = "NOCTIS, built for the Steam Linux Runtime 4 (depot root)";
              platforms = [ system ];
            };
          };

          # `nix run`: the release binary in the same container Steam uses.
          play = pkgs.writeShellScriptBin "noctis" ''
            exec ${run}/bin/steamrt4-run ${release}/noctis "$@"
          '';

          # A conventional Nix build against nixpkgs' SDL3, for tools that are
          # easier to use outside the container (valgrind, sanitizers).
          native = pkgs.stdenv.mkDerivation {
            pname = "noctis";
            inherit version;
            inherit src;
            nativeBuildInputs = [ pkgs.pkg-config ];
            buildInputs = [ pkgs.sdl3 ];
            makeFlags = [ "PREFIX=$(out)" ];
            enableParallelBuilding = true;
            meta = {
              description = "Retro gothic raycasting shooter in plain C (native Nix build)";
              mainProgram = "noctis";
              platforms = systems;
            };
          };
        in
        { inherit sdk sdkSysroot steamLinuxRuntime run release play native; };
    in
    {
      packages = forAllSystems (system: pkgs:
        let p = perSystem system pkgs; in {
          default = p.release;
          inherit (p) release native play;
          steamrt4-sdk = p.sdk;
          steamrt4-run = p.run;
        });

      apps = forAllSystems (system: pkgs:
        let p = perSystem system pkgs; in {
          default = { type = "app"; program = "${p.play}/bin/noctis"; };
          native = { type = "app"; program = "${p.native}/bin/noctis"; };
        });

      checks = forAllSystems (system: pkgs: {
        release = self.packages.${system}.release;
      });

      devShells = forAllSystems (system: pkgs:
        let p = perSystem system pkgs; in {
          # Default: compile with the Steam Linux Runtime 4 SDK and run in the
          # Steam Linux Runtime 4 container, exactly like the release.
          default = pkgs.mkShellNoCC {
            packages = [ p.sdk p.run pkgs.gnumake pkgs.imagemagick ];
            CC = "steamrt4-sdk gcc";
            PKG_CONFIG = "steamrt4-sdk pkg-config";
            RUNNER = "steamrt4-run";
            shellHook = ''
              export STEAMRT4_SDK_BIND=$(git rev-parse --show-toplevel 2>/dev/null || echo "$PWD")
              echo "NOCTIS dev shell (Steam Linux Runtime ${steamrt.version})"
              echo "  make run          build with the SDK, play inside the runtime container"
              echo "  make shots        headless screenshots in build/"
              echo "  steamrt4-sdk CMD  run any SDK tool (gdb, readelf, ...)"
            '';
          };

          # Plain Nix toolchain + nixpkgs SDL3, for valgrind, sanitizers, clangd.
          native = pkgs.mkShell {
            packages = with pkgs; [
              gcc
              clang
              clang-tools   # clangd, clang-format
              gnumake
              pkg-config
              sdl3
              gdb
              valgrind
              bear          # compile_commands.json: `bear -- make`
              imagemagick   # convert --shot PPM screenshots to PNG
            ];
            shellHook = ''
              echo "NOCTIS native dev shell (nixpkgs SDL3) - 'make run' to play"
            '';
          };
        });
    };
}
