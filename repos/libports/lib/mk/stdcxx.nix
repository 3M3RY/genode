{ linkSharedLibrary, compileCCRepo, fromDir, stdcxxSrc, libc, libm }:

let
  internalInclude = ../../include/stdcxx;
  externalInclude = stdcxxSrc.include + "/stdcxx";

  externalIncludes =
    [ internalInclude (internalInclude + "/bits")
      externalInclude
      "${externalInclude}/bits"
      "${externalInclude}/std"
      "${externalInclude}/c_std"
      "${externalInclude}/c_global"

      "${stdcxxSrc.source}/libsupc++"
    ];

  headers = fromDir "${stdcxxSrc.include}/stdcxx"
    [ "bits/stl_algobase.h" 
      "bits/allocator.h"
      "ext/atomicity.h"
      "bits/localefwd.h"
      "config/basic_file_stdio.h"
   ];
in
linkSharedLibrary rec {
  name = "stdcxx";

  libs = [ libc libm ];

  externalObjects = compileCCRepo rec {
    inherit headers libs externalIncludes;
    filter = [ "hash-long-double-tr1-aux.cc" "strstream.cc" ];
    sources = fromDir stdcxxSrc (
      [ # libstdc++ sources
        "src/c++98/*.cc"
        "src/c++11/*.cc"

        # config/locale/generic sources
        "config/locale/generic/*.cc"

        # config/os/generic sources
        "config/os/generic/*.cc"

        # config/io backend
        "config/io/basic_file_stdio.cc"

        # bits of libsupc++
        # (most parts are already contained in the cxx library)
      ] ++
      ( fromDir "libsupc++"
        [ "new_op.cc" "new_opnt.cc" "new_opv.cc" "new_opvnt.cc" "new_handler.cc"
          "del_op.cc" "del_opnt.cc" "del_opv.cc" "del_opvnt.cc"
          "bad_cast.cc" "bad_alloc.cc" "bad_typeid.cc"
          "eh_aux_runtime.cc" "hash_bytes.cc"
          "tinfo.cc"
        ]
      )
    );

    extraFlags =
      [ "-D__GXX_EXPERIMENTAL_CXX0X__" "-std=c++11"

        # TODO: the following flags should propagate

        # prevent gcc headers from defining mbstate
        "-D_GLIBCXX_HAVE_MBSTATE_T"

        # use compiler-builtin atomic operations
        "-D_GLIBCXX_ATOMIC_BUILTINS_4"
      ];
  };

  propagate = { inherit headers externalIncludes; };

}
