# export PATH=$PATH:/Applications/Inkscape.app/Contents/MacOS
# alias inkscape=/Applications/Inkscape.app/Contents/MacOS/inkscape

card_names() {
  rm -rf card_names
  mkdir card_names

  card_name() {
    N=$1
    side=$2
    T=$3
    if [ "$side" == "0" ]; then
      CX=19
      CY=21.5
      TTX=39
      TTY=13.7
      A=start
    elif [ "$side" == "1" ]; then
      CX=`bc <<< "200-19"`
      CY=21.5
      TTX=1
      TTY=13.7
      A=end
    elif [ "$side" == "2" ]; then
      CX=19
      CY=`bc <<< "40-21.5"`
      TTX=39
      TTY=8   # Baseline dependent
      A=start
    elif [ "$side" == "3" ]; then
      CX=`bc <<< "200-19"`
      CY=`bc <<< "40-21.5"`
      TTX=1
      TTY=8
      A=end
    else
      echo "Unknown side!"
    fi
    CTX=`bc <<< "$CX - 15"`   # w/2
    CTY=`bc <<< "$CY - 8.5"`  # Baseline dependent
    cat card_name_templ.svg \
      | perl -pe "s/{N}/${N}/g" | perl -pe "s/{T}/${T}/g" \
      | perl -pe "s/{A}/${A}/g" \
      | perl -pe "s/{CX}/${CX}/g" | perl -pe "s/{CY}/${CY}/g" \
      | perl -pe "s/{CTX}/${CTX}/g" | perl -pe "s/{CTY}/${CTY}/g" \
      | perl -pe "s/{TTX}/${TTX}/g" | perl -pe "s/{TTY}/${TTY}/g" \
      | inkscape --export-filename=$PWD/card_names/$1.png -p
      # > $PWD/card_names/$1.svg  # For inspection
  }

  card_name  1 0 Rider
  card_name  2 0 Clover
  card_name  3 0 Ship
  card_name  4 0 House
  card_name  5 0 Tree
  card_name  6 0 Clouds
  card_name  7 0 Snake
  card_name  8 2 Coffin
  card_name  9 3 Bouquet
  card_name 10 0 Scythe
  card_name 11 0 Whip
  card_name 12 0 Birds
  card_name 13 0 Child
  card_name 14 0 Fox
  card_name 15 0 Bear
  card_name 16 0 Stars
  card_name 17 0 Stork
  card_name 18 0 Dog
  card_name 19 0 Tower
  card_name 20 0 Garden
  card_name 21 0 Mountain
  card_name 22 0 Crossroads
  card_name 23 0 Mice
  card_name 24 0 Heart
  card_name 25 0 Ring
  card_name 26 0 Book
  card_name 27 0 Letter
  card_name 28 0 Guest
  card_name 29 0 Host
  card_name 30 0 Lily
  card_name 31 0 Sun
  card_name 32 0 Moon
  card_name 33 0 Key
  card_name 34 0 Fish
  card_name 35 0 Anchor
  card_name 36 0 Cross
}

card_names
