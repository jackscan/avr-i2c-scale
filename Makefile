-include toolchain.mk

DEVICE     = attiny804
CLOCK      = 3333333UL

OBJECTS    = main.o debug.o

DEFINES    = -DF_CPU=$(CLOCK)

TARGET     = i2c-scale

COMPILE = avr-gcc $(COMPILERARGS) -std=gnu99 -g -Werror -Wall -Wno-unused-function -Os $(DEFINES) \
                  -mmcu=$(DEVICE) -fshort-enums
OBJDUMP = avr-objdump

PYMCUPROG = pymcuprog -d $(DEVICE) $(PYMCUPROG_UART)

all: $(TARGET).hex

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
	rm -f $(TARGET).hex $(TARGET).elf $(OBJECTS)

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

$(OBJECTS): debug.h config.h util.h Makefile

.PHONY: FORCE
FORCE:
