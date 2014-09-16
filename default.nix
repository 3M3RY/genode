/*
 * \brief  Entry expression
 * \author Emery Hemingway
 * \date   2014-09-16
 */

{ system ? builtins.currentSystem }:

let

  nixpkgs = import <nixpkgs> {};

  build = import ./tool/build { inherit system nixpkgs; };

  baseIncludes = import ./repos/base/include { inherit build; };
  osIncludes   = import ./repos/os/include   { inherit build; };
  demoIncludes = import ./repos/demo/include { inherit build; };

  libs = import ./libs.nix {
    inherit build baseIncludes osIncludes demoIncludes;
  };

  test = import ./test.nix {
    inherit build libs baseIncludes osIncludes demoIncludes;
  };

in rec {

  pkgs = import ./pkgs.nix {
    inherit build libs baseIncludes osIncludes demoIncludes;
  };

  run = import ./run.nix {
    inherit system nixpkgs pkgs test;
  };

}