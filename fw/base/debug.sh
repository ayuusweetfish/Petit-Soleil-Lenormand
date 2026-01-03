#!/bin/bash

MONITOR_RESET="monitor reset halt"

if [ "$1" == "c" ]; then
  MONITOR_RESET=
  echo "Attaching without reset"
fi

F=$(mktemp)
cat >$F <<EOF
define hook-quit
  set confirm off
end
define hook-run
  set confirm off
end
define hookpost-run
  set confirm on
end
set pagination off
target extended-remote localhost:3333

define r
  monitor reset halt
  c
end

b debug_trap_line
commands
  silent
  printf "%s\n", (char *)debug_buf
  c
end
${MONITOR_RESET}
c
EOF

make debug &
pid_ocd=$!

~/tools/xpack-arm-none-eabi-gcc-14.2.1-1.1/bin/arm-none-eabi-gdb ${BDIR:-build}/app.elf -x $F
rm $F

kill $pid_ocd 2>/dev/null
