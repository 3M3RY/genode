{ build }:

# no library dependencies
{ }:

build.library {
  name = "blit";
  sources = [ ./blit.cc ];
  includeDirs = 
    if build.isArm then [ ./arm ] else
    if build.isx86_32 then [ ./x86 ./x86/x86_32 ] else
    if build.isx86_64 then [ ./x86 ./x86/x86_64 ] else
    throw "no blit library for build.system";
}