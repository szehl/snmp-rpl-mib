all:
	${MAKE} TARGET=avr-ravenusb MIB_INIT=mib-init-ravenusb.c NOAVRSIZE=1 -f Makefile.ravenusbstick ravenusbstick.elf 
	avr-objcopy -O ihex -R .signature -R .fuse -R .eeprom ravenusbstick.elf ravenusbstick.hex
	avr-objcopy -O ihex -I elf32-avr -j .eeprom ravenusbstick.elf ravenusbstick.eeprom
	avr-size -C --mcu=at90usb1287 ravenusbstick.elf

clean:
	${MAKE} -f Makefile.ravenusbstick clean
	rm -rf obj_avr-ravenusb
	rm symbols.c symbols.h ravenusbstick.elf ravenusbstick.hex ravenusbstick.eeprom

upload_flash:
	avrdude -c jtagmkII -P usb -p usb1287 -Uflash:w:ravenusbstick.hex
upload_eeprom:
	avrdude -c jtagmkII -P usb -p usb1287 -Ueeprom:w:ravenusbstick.eeprom
