#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := Audio
SUFFIX := $(shell components/ESP32-RevK/buildsuffix)
export SUFFIX

all:	settings.h
	@echo Make: build/$(PROJECT_NAME)$(SUFFIX).bin
	@idf.py build
	@cp build/$(PROJECT_NAME).bin $(PROJECT_NAME)$(SUFFIX).bin
	@cp build/bootloader/bootloader.bin $(PROJECT_NAME)$(SUFFIX)-bootloader.bin
	@cp build/ota_data_initial.bin $(PROJECT_NAME)$(SUFFIX)-ota_data_initial.bin
	@cp build/partition_table/partition-table.bin $(PROJECT_NAME)$(SUFFIX)-partition-table.bin
	@echo Done: build/$(PROJECT_NAME)$(SUFFIX).bin

beta:  
	-git pull
	-git submodule update --recursive
	-git commit -a
	@make set
	cp Audio*.bin betarelease
	git commit -a -m betarelease
	git push

issue:  
	-git pull
	-git submodule update --recursive
	-git commit -a
	@make set
	cp Audio*.bin release
	cp Audio*.bin betarelease
	git commit -a -m release
	git push

image:
	make s3
	make flash
	#esptool.py -b 460800 read_flash --flash_size 4MB 0 0x400000 s3.bin
	esptool.py -b 460800 read_flash --flash_size 4MB 0 0x200000 s3.bin

set:	solo wroom pico s3

settings.h:     components/ESP32-RevK/revk_settings settings.def components/ESP32-RevK/settings.def
	components/ESP32-RevK/revk_settings $^

components/ESP32-RevK/revk_settings: components/ESP32-RevK/revk_settings.c
	make -C components/ESP32-RevK
s3:
	components/ESP32-RevK/setbuildsuffix -S3-MINI-N4-R2
	@make

pico:
	components/ESP32-RevK/setbuildsuffix -S1-PICO
	@make

wroom:
	components/ESP32-RevK/setbuildsuffix -S1
	@make

solo:
	components/ESP32-RevK/setbuildsuffix -S1-SOLO
	@make

flash:
	idf.py flash

monitor:
	idf.py monitor

clean:
	idf.py clean

menuconfig:
	idf.py menuconfig

pull:
	git pull
	git submodule update --recursive

update:
	-git pull
	-git commit -a
	git submodule update --init --recursive --remote
	-git commit -a -m "Library update"
