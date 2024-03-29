# IN_PATH=~/Downloads/ChillYunmoGothicMedium.otf OUT_PATH=ChillYunmoGothicMedium_scaled.ttf /Applications/FontForge.app/Contents/Resources/opt/local/bin/fontforge -lang=py -script %
import fontforge
import psMat
import os

input_path = os.environ['IN_PATH']
output_path = os.environ['OUT_PATH']

f = fontforge.open(input_path)

for c in f.selection.all():
  glyph = f.createChar(c)
  glyph.transform(psMat.scale(1.1, 1.0))

f.generate(output_path)

