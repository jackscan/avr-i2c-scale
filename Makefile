-include toolchain.mk

DEVICE     = attiny804
CLOCK      = 3333333UL

OBJECTS    = main.o debug.o hx711.o buckets.o twi.o nvm.o timer.o util.o

DEFINES    = -DF_CPU=$(CLOCK)

TARGET     = i2c-scale

COMPILE = avr-gcc $(COMPILERARGS) -std=gnu99 -g -Werror -Wall -Wno-unused-function -Os $(DEFINES) \
                  -mmcu=$(DEVICE) -fshort-enums
OBJDUMP = avr-objdump

PYMCUPROG = pymcuprog -d $(DEVICE) $(PYMCUPROG_UART)

GIT_TAG := $(shell git describe --tags --abbrev=0  --always)

all: $(TARGET).hex

version.h.tmp: FORCE
	@echo "#ifndef GIT_HASH" > version.h.tmp
	@echo "#define VERSION_MAJOR $(word 1,$(subst ., ,$(GIT_TAG)))" >> version.h.tmp
	@echo "#define VERSION_MINOR $(word 2,$(subst ., ,$(GIT_TAG)))" >> version.h.tmp
	@echo "#define VERSION_PATCH $(shell git log --oneline $(GIT_TAG)..HEAD| wc -l)" >> version.h.tmp
	@echo '#define GIT_HASH 0x$(shell git rev-parse --short=4 --abbrev-commit HEAD)' >> version.h.tmp
	@echo "#define GIT_DIRTY $(shell git diff --quiet; echo $$?)" >> version.h.tmp
	@echo "#endif" >> version.h.tmp

version.h: version.h.tmp
	@diff $@ $< && rm $< || mv $< $@

.c.o:
	$(COMPILE) -c $< -o $@

.S.o:
	$(COMPILE) -x assembler-with-cpp -c $< -o $@

.c.s:
	$(COMPILE) -S $< -o $@

flash: $(TARGET).hex
	$(PYMCUPROG) write -f $< --erase

ping:
	$(PYMCUPROG) ping

clean:
	rm -f $(TARGET).hex $(TARGET).elf $(OBJECTS) version.h

$(TARGET).elf: $(OBJECTS)
	$(COMPILE) -o $(TARGET).elf $^

%.hex: %.elf
	rm -f $@
	avr-objcopy -j .text -j .data -O ihex $< $@
	avr-size --format=avr --mcu=$(DEVICE) $<

%.eep: %.elf
	rm -f $@
	avr-objcopy -j .eeprom --change-section-lma .eeprom=0 -O ihex $< $@

disasm: $(TARGET).elf
	avr-objdump -d $<

cpp:
	$(COMPILE) -E main.c

%.lst: %.elf
	$(OBJDUMP) -h -S $< > $@

$(OBJECTS): debug.h config.h util.h version.h hx711.h buckets.h twi.h nvm.h timer.h Makefile

.PHONY: FORCE
FORCE:
