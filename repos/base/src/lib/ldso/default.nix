{ genodeEnv, linkSharedLibrary, compileCC, compiles, repoDir
, baseLibs, ldso-startup }:

let
  archDir =
    if genodeEnv.isArm    then ./arm    else
    if genodeEnv.isx86_32 then ./x86_32 else
    if genodeEnv.isx86_64 then ./x86_64 else
    throw "ld expression incomplete for ${genodeEnv.system}";

  compileCC' = src: compileCC {
    inherit src;
    systemIncludes = [ archDir ./include ];
  };

  kernelLdFlags =
    if genodeEnv.isLinux
    then [ ("-T" + (repoDir + "/src/platform/context_area.nostdlib.ld")) ]
    else [ "-T${./linker.ld}" ];
in
linkSharedLibrary {
  name = "ld";
  libs = baseLibs;
  objects =
    ( map compileCC' [ ./main.cc ./test.cc ./file.cc ] ) ++
    [ ( compiles { src = archDir + "/jmp_slot.s"; } ) ];

  entryPoint = "_start";

  extraLdFlags =
    [ "-Bsymbolic-functions" "--version-script=${./symbol.map}" ] ++
    kernelLdFlags;
}
