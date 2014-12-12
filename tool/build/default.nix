/*
 * \brief  Build environment
 * \author Emery Hemingway
 * \date   2014-08-23
 */

{ system, tool }:

with tool;

let
  devPrefix = "genode-${spec.platform}-";

  # TODO deduplicate with ../build/genode-env.nix
  # attributes that should always be present
  stdAttrs = rec
    { verbose = 1;

      cc      = "${devPrefix}gcc";
      cxx     = "${devPrefix}g++";
      ld      = "${devPrefix}ld";
      as      = "${devPrefix}as";
      ar      = "${devPrefix}ar";
      nm      = "${devPrefix}nm";
      objcopy = "${devPrefix}objcopy";
      ranlib  = "${devPrefix}ranlib";
      strip   = "${devPrefix}strip";

      ccMarch = spec.ccMarch or [];
      ldMarch = spec.ldMarch or [];
      asMarch = spec.ldMarch or [];

      ccOLevel = "-O2";
      ccWarn   = "-Wall";

      ccFlags =
        (spec.ccFlags or []) ++ spec.ccMarch ++
        [ ccOLevel ccWarn

          # Always compile with '-ffunction-sections' to enable
          # the use of the linker option '-gc-sections'
          "-ffunction-sections"

          # Prevent the compiler from optimizations
          # related to strict aliasing
          "-fno-strict-aliasing"

          # Do not compile with standard includes per default.
          "-nostdinc"

          # It shouldn't be too difficult to make a function to
          # turn this back on for specific components.
          #"-g"
        ];

      cxxFlags = [ "-std=gnu++11" ];

      ccCFlags = ccFlags;

      ldFlags = ldMarch ++ [ "-gc-sections" ];

      asFlags = spec.asMarch;

      cxxLinkFlags = [ "-nostdlib -Wl,-nostdlib"] ++ spec.ccMarch;

      nativeIncludes =
        [ "${toolchain}/lib/gcc/${spec.target}/${toolchain.version}/include"
        ];
    };

  # Linker script for dynamically linked programs
  ldScriptDyn = ../../repos/base/src/platform/genode_dyn.ld;


  componentLinkPhase = ''
    echo -e "    LINK     $name"

    for o in $externalObjects
    do objects="$objects $o/*.o"
    done

    objects=$(sortWords $objects)

    local _libs=$libs
    local libs=""

    for l in $_libs
    do findLibs $l libs
    done

    libs=$(sortDerivations $libs)

    mkdir -p $out

    [[ "$ldTextAddr" ]] && \
        cxxLinkFlags="$cxxLinkFlags -Wl,-Ttext=$ldTextAddr"

    for s in $ldScripts
    do cxxLinkFlags="$cxxLinkFlags -Wl,-T -Wl,$s"
    done

    VERBOSE $cxx $cxxLinkFlags \
	-Wl,--whole-archive -Wl,--start-group \
        $objects $libs \
	-Wl,--end-group -Wl,--no-whole-archive \
        $($cc $ccMarch -print-libgcc-file-name) \
        -o $out/$name
  '';

  componentFixupPhase = " ";

  spec =
    if system == "i686-linux" then import ../../specs/x86_32-linux.nix else
    if system == "x86_64-linux" then import ../../specs/x86_64-linux.nix else
    if system == "x86_32-nova"  then import ../../specs/x86_32-nova.nix  else
    if system == "x86_64-nova"  then import ../../specs/x86_64-nova.nix  else
    abort "unknown system type ${system}";

  toolchain = nixpkgs.callPackage ../toolchain/precompiled {};

  propagateCompileArgs = args:
    let
      libs = findLinkLibraries args.libs or [];
    in
    (removeAttrs args [ "libs" ]) //
    { extraFlags =
        args.extraFlags or [] ++
        (propagate "propagatedFlags" libs);

      systemIncludes =
        args.systemIncludes or [] ++
        (propagate "propagatedIncludes" libs) ++
        stdAttrs.nativeIncludes;
    };

in
rec {
  # Utility flags to test the type of platform.
  is32Bit  = spec.bits == 32;
  is64Bit  = spec.bits == 64;
  isArm = spec.platform == "arm";
  isx86 = spec.platform == "x86";
  isx86_32 = isx86 && is32Bit;
  isx86_64 = isx86 && is64Bit;

  isLinux = spec.kernel == "linux";
  isNova  = spec.kernel == "nova";
  isNOVA  = isNova;

  genodeEnv = import ./genode-env.nix {
     inherit system tool spec stdAttrs;
  };
  genodeEnvAdapters = import ./adapters.nix;

  ## TODO
  # Deduplicate the following functions.
  # It shouldn't be hard, the scripts can
  # be broken apart into files to be sourced
  # by other scripts.

  compiles =
  { src
  , localIncludes ? []
  , ... } @ args:
  shellDerivation (
    { inherit (stdAttrs) cc ccFlags;
      inherit genodeEnv;
    } //
    args //
    {
      name = dropSuffix ".s" (baseNameOf (toString src)) + ".o";
      script = ./compile-s.sh;
      localIncludes = findLocalIncludes src localIncludes;
    }
  );

  compileS =
  { src
  , localIncludes ? []
  , ... } @ args:
  shellDerivation (
    { inherit (stdAttrs) cc ccFlags;
      inherit genodeEnv;
    } //
    args //
    {
      name = dropSuffix ".S" (baseNameOf (toString src)) + ".o";
      script = ./compile-S.sh;
      localIncludes = findLocalIncludes src localIncludes;
    }
  );

  compileC =
  { src
  , localIncludes ? []
  , ... } @ args:
  shellDerivation ( (propagateCompileArgs args) //
    { inherit (stdAttrs) cc ccFlags;
      inherit genodeEnv;
      name = dropSuffix ".c" (baseNameOf (toString src)) + ".o";
      script = ./compile-c.sh;
      localIncludes = findLocalIncludes src localIncludes;
    }
  );

  compileCC =
  { src
  , localIncludes ? []
  , ... } @ args:
  shellDerivation ( (propagateCompileArgs args) //
    { inherit (stdAttrs) cxx ccFlags cxxFlags;
      inherit genodeEnv;
      name = dropSuffix ".cc" (baseNameOf (toString src)) + ".o";
      script = ./compile-cc.sh;
      localIncludes = findLocalIncludes src localIncludes;
    }
  );

  transformBinary =
  binary:
  let
    s = baseNameOf (toString binary);
    fixName = s:
      let
        l = builtins.stringLength s;
        c = builtins.substring 0 1 s;
      in
      if l == 0 then "" else
        (if c == "." || c == "-" then "_" else c) +
        fixName (builtins.substring 1 l s);
    symbolName = "_binary_" + fixName s;
  in
  shellDerivation {
    name =
      if builtins.typeOf binary == "path"
      then symbolName + ".o"
      else "binary";
    script = ./transform-binary.sh;
    inherit genodeEnv binary symbolName;
    inherit (stdAttrs) as asFlags;
  };

  # Compile objects from a port derivation.
  compileCRepo =
  { name ? "objects"
  , sources
  , ... } @ args:
  shellDerivation ( (propagateCompileArgs args) //
    { inherit name genodeEnv;
      inherit (stdAttrs) cc ccFlags;
      script = ./compile-c-port.sh;
    }
  );

  # Compile objects from a port derivation.
  compileCCRepo =
  { name ? "objects"
  , sources
  , ... } @ args:
  shellDerivation ( (propagateCompileArgs args) //
    { inherit name genodeEnv;
      inherit (stdAttrs) cxx ccFlags cxxFlags;
      script = ./compile-cc-port.sh;
    }
  );

  # Link together a static library.
  linkStaticLibrary =
  { libs ? [], ... } @ args:
  shellDerivation ( args // {
    script = ./link-static-library.sh;
    inherit genodeEnv;
    inherit (stdAttrs) ar ld ldFlags cxx objcopy;
  }) // { shared = false; inherit libs; };

  # Link together a shared library.
  # This function must be preloaded with an ldso-startup library.
  linkSharedLibrary =
  { ldso-startup }:
  { ldScriptSo ? ../../repos/base/src/platform/genode_rel.ld
  , entryPoint ? "0x0"
  , libs ? []
  , ... } @ args:
  shellDerivation ( args // {
    script = ./link-shared-library.sh;
    libs = [ ldso-startup ] ++ libs;
    inherit genodeEnv ldScriptSo entryPoint;
    inherit (stdAttrs) ld ldFlags cc ccMarch;
  }) // { shared = true; };

  # Link together a component with static libraries.
  linkStaticComponent =
  { ldTextAddr ? spec.ldTextAddr
  , ldScripts ? [ ../../repos/base/src/platform/genode.ld ]
  , libs ? []
  , ... } @ args:
  let
    libs' = libs;
  in
  shellDerivation ( args // {
    script = ./link-static-component.sh;
    inherit genodeEnv ldTextAddr ldScripts;
    inherit (stdAttrs) ld ldFlags cxx cxxLinkFlags cc ccMarch;
    libs = findLinkLibraries libs';
  }) // { libs = findRuntimeLibraries libs'; };

  # Link together a component with shared libraries.
  linkDynamicComponent =
  { dynamicLinker
  ,  dynDl ? ../../repos/base/src/platform/genode_dyn.dl
  , ldTextAddr ? spec.ldTextAddr
  , ldScripts ? [ ../../repos/base/src/platform/genode_dyn.ld ]
  , libs ? []
  , ... } @ args:
  let
    libs' = libs;
  in
  shellDerivation ( args // {
    script = ./link-dynamic-component.sh;
    inherit genodeEnv dynDl ldTextAddr dynamicLinker ldScripts;
    inherit (stdAttrs) ld ldFlags cxx cxxLinkFlags cc ccMarch;
    libs = (findLinkLibraries libs') ++ [ dynamicLinker ];
  }) // { libs = findRuntimeLibraries libs'; };

}
