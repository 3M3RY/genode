{ tool, run, pkgs }:

#if {[have_spec linux] || [have_spec pistachio]} {
#	puts "Platform does not support managed dataspaces"; exit }

if tool.genodeEnv.spec.kernel == "linux"
then null # "Platform does not support managed dataspaces"
else

run {
  name = "rm_fault";

  contents = [
    { target = "/"; source = pkgs.test.rm_fault; }
    { target = "config";
      source = builtins.toFile "config" ''
        <config>
          <parent-provides>
            <service name="ROM"/>
            <service name="RAM"/>
            <service name="CPU"/>
            <service name="RM"/>
            <service name="CAP"/>
            <service name="PD"/>
            <service name="SIGNAL"/>
            <service name="LOG"/>
          </parent-provides>
          <default-route>
            <any-service> <parent/> </any-service>
          </default-route>
          <start name="test-rm_fault">
            <resource name="RAM" quantum="10M"/>
          </start>
        </config>
      '';
    }
  ];

  testScript =
    ''
       append qemu_args "-nographic -m 64"

       run_genode_until {child "test-rm_fault" exited with exit value 0.*} 10

       puts "Test succeeded"
    '';
}
