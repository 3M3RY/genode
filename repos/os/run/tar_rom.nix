#
# \brief  Test for 'tar_rom' service
# \author Norman Feske
# \date   2010-09-07
#
# The test spawns a sub init, which uses a 'tar_rom' instance
# rather than core's ROM service. The 'tar_rom' service manages
# a TAR archive containing the binary of the 'test-timer' program.
# The nested init instance tries to start this program. The
# test succeeds when the test-timer program prints its first
# line of LOG output.
#

{ tool, run, pkgs }:
#
# On Linux, programs can be executed only if present as a file on the Linux
# file system ('execve' takes a file name as argument). Data extracted via
# 'tar_rom' is not represented as file. Hence, it cannot be executed.
#
#if {[have_spec linux]} { puts "Run script does not support Linux"; exit 0 }

let
  archive = tool.shellDerivation {
    name = "archive.tar";
    inherit (tool) genodeEnv;
    inherit (pkgs.test) timer;
    inherit (tool.nixpkgs) gnutar;
    script = builtins.toFile "archive-builder.sh"
      ''
        source $genodeEnv/setup

        cp $timer/* ./
        $gnutar/bin/tar cfh $out test-timer
      '';#*/
  };
in
run {
  name = "tar_rom";

  contents = [
    { target = "/"; source = pkgs.drivers.timer; }
    { target = "/"; source = pkgs.server.tar_rom; }
    { target = "/archive.tar"; source = archive; }
    { target = "/config";
      source = builtins.toFile "config" ''
        <config>
	<parent-provides>
		<service name="ROM"/>
		<service name="RAM"/>
		<service name="IRQ"/>
		<service name="IO_MEM"/>
		<service name="IO_PORT"/>
		<service name="CAP"/>
		<service name="PD"/>
		<service name="RM"/>
		<service name="CPU"/>
		<service name="LOG"/>
		<service name="SIGNAL"/>
	</parent-provides>
	<default-route>
		<any-service> <parent/> <any-child/> </any-service>
	</default-route>
	<start name="timer">
		<resource name="RAM" quantum="1M"/>
		<provides><service name="Timer"/></provides>
	</start>
	<start name="tar_rom">
		<resource name="RAM" quantum="5200K"/>
		<provides><service name="ROM"/></provides>
		<config>
			<archive name="archive.tar"/>
		</config>
	</start>
	<start name="init">
		<resource name="RAM" quantum="2M"/>
		<config verbose="yes">
			<parent-provides>
				<service name="ROM"/>
				<service name="RM"/>
				<service name="RAM"/>
				<service name="LOG"/>
				<service name="Timer"/>
			</parent-provides>
			<start name="test-timer">
				<resource name="RAM" quantum="1M"/>
				<route> <any-service> <parent/> </any-service> </route>
			</start>
		</config>
		<route>
			<any-service> <child name="tar_rom"/> <parent/> <any-child/> </any-service>
		</route>
	</start>
        </config>
      '';
    }
  ];

  testScript =
    ''
      append qemu_args "-nographic -m 64"

      run_genode_until "--- timer test ---" 10

      puts "Test succeeded"
    '';
}
