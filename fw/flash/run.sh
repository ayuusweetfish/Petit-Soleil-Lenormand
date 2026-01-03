WD=$(mktemp -d --tmpdir soleil.XXX)
echo $WD

cc gen.c -O2 -o $WD/gen
$WD/gen ../misc/bitmap_font/wenquanyi_9pt.bin ../../cards/cards.bin $WD/flash.gdbinit
