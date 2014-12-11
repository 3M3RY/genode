{ run, pkgs }:

run {
  name = "xml_generator";

  contents = [
    { target = "/"; source = pkgs.test.xml_generator; }
    { target = "/config";
      source = builtins.toFile "config" ''
	<config>
		<parent-provides>
			<service name="ROM"/>
			<service name="LOG"/>
			<service name="CAP"/>
			<service name="CPU"/>
			<service name="RAM"/>
			<service name="RM"/>
			<service name="PD"/>
			<service name="SIGNAL"/>
		</parent-provides>
		<default-route>
			<any-service> <any-child/> <parent/> </any-service>
		</default-route>
		<start name="test-xml_generator">
			<resource name="RAM" quantum="1M"/>
		</start>
	</config>
      '';
    }
  ];

  testScript =
    ''
      append qemu_args "-nographic -m 64"

      run_genode_until "--- XML generator test finished ---.*\n" 30

      grep_output {^\[init -> test-xml_generator}

compare_output_to {
	[init -> test-xml_generator] --- XML generator test started ---
	[init -> test-xml_generator] result:
	[init -> test-xml_generator]
	[init -> test-xml_generator] <config xpos="27" ypos="34" verbose="true">
	[init -> test-xml_generator] 	<box width="320" height="240"/>
	[init -> test-xml_generator] 	<label name="a test">
	[init -> test-xml_generator] 		<sub_label/>
	[init -> test-xml_generator] 		<another_sub_label>
	[init -> test-xml_generator] 			<sub_sub_label/>
	[init -> test-xml_generator] 		</another_sub_label>
	[init -> test-xml_generator] 	</label>
	[init -> test-xml_generator] </config>
	[init -> test-xml_generator]
	[init -> test-xml_generator] used 199 bytes
	[init -> test-xml_generator] buffer exceeded (expected error)
	[init -> test-xml_generator] --- XML generator test finished ---
}
    '';
}
