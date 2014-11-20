/*
 * \brief  Portions of base library that are exclusive to non-core processes
 * \author Norman Feske
 * \date   2013-02-14
 */

{ genodeEnv, baseDir, repoDir, base-common }:

with genodeEnv.tool;

genodeEnv.mkLibrary {
  name = "base";
  libs = [ base-common ];
  sources =
    fromDir (repoDir + "/src/base") [ "thread/thread_nova.cc" ]
    ++
    fromDir (baseDir+"/src/base")
      [ "console/log_console.cc"
        "cpu/cache.cc"
        "env/context_area.cc"
        "env/env.cc"
        "env/reinitialize.cc"
      ];

  systemIncludes = [ (baseDir + "/src/base/env") ];

}
