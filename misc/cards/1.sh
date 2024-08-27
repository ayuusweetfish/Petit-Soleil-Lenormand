convert card_name_34.png -background white -flatten -alpha off -threshold 62.5% -negate R:- | ../../fw/misc/rle
convert card_name_34.png -alpha extract -threshold 62.5% R:- | ../../fw/misc/rle
