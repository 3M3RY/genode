/*
 * \author Emery Hemingway
 * \date   2014-09-19
 */

{ preparePort, fetchsvn }:

let rev = "3837"; in
preparePort rec {
  name = "dosbox-r${rev}";

  src = fetchsvn {
    url = http://svn.code.sf.net/p/dosbox/code-0/dosbox/trunk;
    inherit rev;
    sha256 = "1difbi5civmp9xjbcljcnhbjl7zwqqlkhhldghq46jbfs5g4rs0y";
  };

  postUnpack = "cd ${name}";
  
  patches = "${../src/app/dosbox/patches}/*.patch";
  patchFlags = "-p2";
}
