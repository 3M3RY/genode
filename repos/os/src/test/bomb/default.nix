{ linkComponent, compileCC, base, config }:

linkComponent {
  name = "bomb";
  libs = [ base config ];
  objects = compileCC { src = ./main.cc; };
}
