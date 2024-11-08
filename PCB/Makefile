all: stl

stl:    Audio/Audio.stl

PCBCase/case: PCBCase/case.c
	make -C PCBCase

%.stl: %.scad
	echo "Making $@"
	/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD $< -o $@
	echo "Made $@"

Audio/Audio.scad: Audio/Audio.kicad_pcb PCBCase/case Makefile
	PCBCase/case -o $@ $< --edge=2 --bottom=2 --top=7 --pcb=2
