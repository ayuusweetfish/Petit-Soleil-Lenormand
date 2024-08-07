# (cd ~/Downloads/stlink; ./build/Release/bin/st-info --probe --connect-under-reset)
# (cd ~/Downloads/stlink; ./build/Release/bin/st-flash --connect-under-reset write $OLDPWD/.pio/build/dev/firmware.bin 0x8000000)
# ~/.platformio/packages/tool-openocd/bin/openocd -f interface/stlink.cfg -f target/stm32g0x.cfg -c 'program {.pio/build/dev/firmware.elf} verify reset; shutdown'

# ~/.platformio/packages/tool-openocd/bin/openocd -f interface/stlink.cfg -f target/stm32g0x.cfg -c 'adapter speed 32000'

cat >debug/gdbinit <<EOF
define hook-quit
  set confirm off
end
target extended-remote localhost:3333

b swv_trap_line
commands
  silent
  printf "%s\n", swv_buf
  c
end
r
EOF

~/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-gdb .pio/build/dev/firmware.elf -x debug/gdbinit
rm debug/gdbinit
