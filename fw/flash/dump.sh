#/bin/bash

# Makes a GDB script to dump the SPI flash content to a temporary file.

SCRIPT=/tmp/soleil.dump.gdbinit
DUMP=/tmp/soleil.dump.bin

> $SCRIPT
> $DUMP

for i in `seq 0 4096 $((2*1024*1024 - 4096))`; do
  offs=`printf "%06x" $i`
  echo "echo 0x$offs\\n" >> $SCRIPT
  echo "call flash_read(0x$offs, flash_test_write_buf, 4096)" >> $SCRIPT
  echo "append binary memory $DUMP flash_test_write_buf flash_test_write_buf+4096" >> $SCRIPT
done

echo "source $SCRIPT"
