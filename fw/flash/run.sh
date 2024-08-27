cc gen.c -O2 -o gen

./gen ../misc/bitmap_font/wenquanyi_9pt.bin ../../misc/cards/cards.bin flash.gdbinit
rm gen

# source flash/flash.gdbinit
