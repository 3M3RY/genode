/*
 * Portion of the string library that is used by both the freestanding string
 * library and the complete libc
 */

{ linkStaticLibrary, compileSubLibc }:
let dir = "lib/libc/string"; in
linkStaticLibrary {
  name = "libc-string";

  externalObjects = compileSubLibc {
    sources = [ "${dir}/*.c" ];
    filter = map
      (fn: "${dir}/${fn}")
      [ "strcoll.c" "strxfrm.c" "wcscoll.c" "wcsxfrm.c" ];
    systemIncludes = [ "${dir}" "include" ];
  };
}
