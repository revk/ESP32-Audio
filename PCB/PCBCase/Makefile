all:	case tracklen PCB/Test/Test.scad

case:	case.c pcb.c models case.scad
	gcc -O -o $@ $< pcb.c -lpopt -lm -g

tracklen:	tracklen.c
	gcc -O -o $@ $< -lpopt -lm -g

PCB/Test/Test.scad:	PCB/Test/Test.kicad_pcb case Makefile
	./case -o $@ $< -M models
