{ linkStaticLibrary, compileCC, compileCRepo
, sqliteSrc, libc, pthread, jitterentropy
, debug ? false }:

let
  objects = compileCC {
    src = ./native_sqlite.cc;
    libs = [ libc jitterentropy ];
    externalIncludes = [ sqliteSrc.include ];
  };
in
import ../sqlite/common.nix {
  inherit
    linkStaticLibrary compileCC compileCRepo
    sqliteSrc libc pthread jitterentropy debug;
} objects