# IN_PATH=~/Downloads/ChillYunmoGothicMedium.otf OUT_PATH=ChillYunmoGothicMedium_subset.ttf TEXT=`cat *.html | perl -CIO -pe 's/[^\N{U+4e00}-\N{U+9fff}\N{U+3400}-\N{U+4dbf}\N{U+3000}-\N{U+303f}\N{U+2000}-\N{U+206f}\N{U+ff00}-\N{U+ffef}\N{U+00b7}]//g'` /Applications/FontForge.app/Contents/Resources/opt/local/bin/fontforge -lang=py -script font-subset.py
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
