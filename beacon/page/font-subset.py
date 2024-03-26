# IN_PATH=~/Downloads/ChillYunmoGothicMedium.otf OUT_PATH=ChillYunmoGothicMedium_subset.ttf TEXT=`cat index.html | perl -CIO -pe 's/[\p{ASCII} \N{U+2500}-\N{U+257F}]//g'` /Applications/FontForge.app/Contents/Resources/opt/local/bin/fontforge -lang=py -script font-subset.py
# woff2_compress ChillYunmoGothicMedium_subset.ttf; rm ChillYunmoGothicMedium_subset.ttf
import fontforge
import psMat
import os

input_path = os.environ['IN_PATH']
text = os.environ['TEXT']
output_path = os.environ['OUT_PATH']

text = sorted(map(ord, set(text)))

f = fontforge.open(input_path)
f.selection.select(('unicode',), *text)
f.selection.invert()
for glyph in f.selection.byGlyphs:
  f.removeGlyph(glyph)

for c in text:
  glyph = f.createChar(c)
  glyph.transform(psMat.scale(1.1, 1.0))

f.generate(output_path)
