# export PATH=$PATH:/Applications/Inkscape.app/Contents/MacOS

rm -rf card_names
mkdir card_names

card_name() {
  cat card_name_templ.svg | perl -pe "s/{N}/$1/g" | perl -pe "s/{T}/$2/g" | inkscape --export-filename=$PWD/card_names/$1.png -p
}

card_name 1 Rider
card_name 2 Clover
card_name 3 Ship
card_name 4 House
card_name 5 Tree
card_name 6 Clouds
card_name 7 Snake
card_name 8 Coffin
card_name 9 Bouquet
card_name 10 Scythe
card_name 11 Whip
card_name 12 Birds
card_name 13 Child
card_name 14 Fox
card_name 15 Bear
card_name 16 Stars
card_name 17 Stork
card_name 18 Dog
card_name 19 Tower
card_name 20 Garden
card_name 21 Mountain
card_name 22 Crossroads
card_name 23 Mice
card_name 24 Heart
card_name 25 Ring
card_name 26 Book
card_name 27 Letter
card_name 28 Animus
card_name 29 Anima
card_name 30 Lily
card_name 31 Sun
card_name 32 Moon
card_name 33 Key
card_name 34 Fish
card_name 35 Anchor
card_name 36 Cross
