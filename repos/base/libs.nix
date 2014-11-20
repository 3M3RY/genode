/*
 * \brief  Base libraries.
 * \author Emery Hemingway
 * \date   2014-09-11
 */

{ tool, callLibrary }:

let

  # Prepare genodeEnv.
  genodeEnv = tool.genodeEnvAdapters.addSystemIncludes
    tool.genodeEnv (import ./include { inherit (tool) genodeEnv; });

  compileCC =
  attrs:
  tool.compileCC (attrs // {
    systemIncludes =
     (attrs.systemIncludes or [])
      ++
      (import ./include { inherit (tool) genodeEnv; });
  });

  callLibrary' = callLibrary {
    inherit genodeEnv compileCC;
    inherit (tool) compileS;
  };
  importLibrary = path: callLibrary' (import path);

  callBaseLibrary = callLibrary {
    inherit genodeEnv compileCC baseDir repoDir;
    inherit (tool) compileS;
  };
  importBaseLibrary = path: callBaseLibrary (import path);

  baseDir = ../base;
  repoDir =
    if tool.genodeEnv.isLinux then ../base-linux else
    if tool.genodeEnv.isNova  then ../base-nova  else
    throw "no base libraries for ${genodeEnv.system}";

  impl =
    import (repoDir+"/libs.nix") { inherit tool importBaseLibrary; };

in 
impl // {
  cxx     = importBaseLibrary ./src/base/cxx;
  startup = importBaseLibrary ./lib/mk/startup.nix;
}
