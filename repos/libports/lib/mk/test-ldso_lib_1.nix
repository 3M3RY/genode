{ genodeEnv, test-ldso_lib_2, ldso-startup }:

genodeEnv.mkLibrary {
  name = "test-ldso_lib_1";
  shared = true;
  libs = [ test-ldso_lib_2 ldso-startup ];
  sources = genodeEnv.fromPath ../../src/test/ldso/lib_1.cc;
  localIncludes = [ ../../src/test/ldso/include ];
}
