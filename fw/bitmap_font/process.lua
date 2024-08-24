-- lua % < wenquanyi_9pt.bdf
-- head -n 1221 wenquanyi_9pt.bdf | lua %
local s = io.read('a')

-- Output format:
--   uint32_t glyph_offs[]
--   uint8_t glyph_data[]
-- Each glyph:
--   uint8_t advance_x
--   uint8_t bbx_offs_x, bbx_offs_y
--   uint16_t bitmap[12]

for encoding, advance_x, advance_y, bbx_w, bbx_h, bbx_x, bbx_y, bitmap in s:gmatch('STARTCHAR%s+[^\n]-\nENCODING%s+([^\n]+).-DWIDTH ([^\n]+) ([^\n]+).-BBX ([^\n]+) ([^\n]+) ([^\n]+) ([^\n]+).-BITMAP.-([0-9A-Fa-f\n]+)ENDCHAR\n') do
  encoding = tonumber(encoding)
  advance_x = tonumber(advance_x)
  advance_y = tonumber(advance_y)
  bbx_w = tonumber(bbx_w)
  bbx_h = tonumber(bbx_h)
  bbx_x = tonumber(bbx_x)
  bbx_y = tonumber(bbx_y)
  -- bbx_y = 10 - bbx_y - bbx_h
  if advance_y ~= 0 then print(encoding, advance_y, 'advance y non-zero!') end
  -- if bbx_x < 0 or bbx_y < 0 then print(encoding, bbx_x, bbx_y, 'bounding box out of range!') end
  -- if bbx_w > 12 or bbx_h > 12 then print(encoding, bbx_w, bbx_h, 'bounding box too large!') end

  local a = {}
  for i in bitmap:gmatch('[0-9A-Fa-f]+') do
    a[#a + 1] = tonumber(i, 16)
  end
end

-- 1234⇡1ijm夜)&
