# Depends: rsvg-convert
# pyftsubset Mali-Bold.ttf --unicodes=30-39,41-5a,61-7a --output-file=fonts/Mali-Bold.ttf

wd="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
card_names_dir="$(readlink -f "$1")"
cd $wd

echo "Script root: $wd"
echo "Card names: $card_names_dir"

card_names() {
  card_name() {
    N=$1    # Number
    side=$2 # Side
    T=$3    # Title
    S=$4    # Letter spacing
    if [ -z "$S" ]; then
      S=0
    fi
    if [ "$side" == "0" ]; then
      CX=19
      CY=21.5
      TTX=39
      TTY=`bc <<< "21.5 + 8.2"`
      A=start
    elif [ "$side" == "1" ]; then
      CX=`bc <<< "200-19"`
      CY=21.5
      TTX=`bc <<< "200-39"`
      TTY=`bc <<< "21.5 + 8.2"`
      A=end
    elif [ "$side" == "2" ]; then
      CX=19
      CY=`bc <<< "40-21.5"`
      TTX=39
      TTY=`bc <<< "21.5 + 3.5"`
      A=start
    elif [ "$side" == "3" ]; then
      CX=`bc <<< "200-19"`
      CY=`bc <<< "40-21.5"`
      TTX=`bc <<< "200-39"`
      TTY=`bc <<< "21.5 + 3.5"`
      A=end
    else
      echo "Unknown side!"
    fi
    CTX=`bc <<< "$CX"`
    CTY=`bc <<< "$CY + 5.75"`
    cat $wd/card_name_templ.svg \
      | perl -pe "s/{N}/${N}/g" | perl -pe "s/{T}/${T}/g" \
      | perl -pe "s/{A}/${A}/g" \
      | perl -pe "s/{CX}/${CX}/g" | perl -pe "s/{CY}/${CY}/g" \
      | perl -pe "s/{CTX}/${CTX}/g" | perl -pe "s/{CTY}/${CTY}/g" \
      | perl -pe "s/{TTX}/${TTX}/g" | perl -pe "s/{TTY}/${TTY}/g" \
      | perl -pe "s/{S}/${S}/g" \
      | FONTCONFIG_FILE=$wd/fonts/fonts.conf rsvg-convert > $card_names_dir/$1.png
      # > /tmp/card_names/$1.svg  # For inspection
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
  card_name 22 0 Crossroads -0.4
  card_name 23 0 Mice
  card_name 24 0 Heart
  card_name 25 0 Ring
  card_name 26 0 Book
  card_name 27 0 Letter
  card_name 28 0 Guest
  card_name 29 0 Host
  card_name 30 0 Lily
  card_name 31 1 Sun
  card_name 32 0 Moon
  card_name 33 0 Key
  card_name 34 0 Fish
  card_name 35 0 Anchor
  card_name 36 0 Cross
}

card_names
