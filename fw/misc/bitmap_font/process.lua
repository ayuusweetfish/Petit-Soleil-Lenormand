-- lua % < wenquanyi_9pt.bdf
-- head -n 1221 wenquanyi_9pt.bdf | lua %
local s = io.read('a')

-- Output format:
--   uint32_t glyph_offs[]
--   uint8_t glyph_data[]
-- Each glyph:
--   uint8_t advance_x
--   int8_t bbx_offs_x, bbx_offs_y
--   uint8_t bitmap[18]
local out_buf = {}

local glyph_offs = {}
for i = 0, 65535 do glyph_offs[i] = -1 end
local n_glyphs = 0

local eprint = function (...)
  local n = select('#', ...)
  for i = 1, n do
    io.stderr:write(tostring(select(i, ...)))
    io.stderr:write(i < n and '\t' or '\n')
  end
end

for encoding, advance_x, advance_y, bbx_w, bbx_h, bbx_x, bbx_y, bitmap in s:gmatch('STARTCHAR%s+[^\n]-\nENCODING%s+([^\n]+).-DWIDTH ([^\n]+) ([^\n]+).-BBX ([^\n]+) ([^\n]+) ([^\n]+) ([^\n]+).-BITMAP.-([0-9A-Fa-f\n]+)ENDCHAR\n') do
  encoding = tonumber(encoding)
  advance_x = tonumber(advance_x)
  advance_y = tonumber(advance_y)
  bbx_w = tonumber(bbx_w)
  bbx_h = tonumber(bbx_h)
  bbx_x = tonumber(bbx_x)
  bbx_y = tonumber(bbx_y)
  if advance_y ~= 0 then eprint(encoding, advance_y, 'advance y non-zero!') end

  local a = {}
  for i in bitmap:gmatch('[0-9A-Fa-f]+') do
    if #i % 2 ~= 0 or #i > 4 then eprint(encoding, i, 'invalid bitmap format') end
    -- Zero-pad at the end to 16 bits
    if #i < 4 then i = i .. '00' end
    i = tonumber(i, 16)
    -- Assert last 4 (= 16 - 12) bits are zero
    if i % 16 ~= 0 then eprint(encoding, i, 'excessive bits in bitmap') end
    a[#a + 1] = i
  end

  -- Trim whitespaces and excessive rows to get around excessive boxes
  while #a > 1 and a[#a] == 0 do
    a[#a] = nil
    bbx_h = bbx_h - 1
    bbx_y = bbx_y + 1
  end
  while #a > 1 and (a[1] == 0 or #a > 12) do
    table.remove(a, 1)
    bbx_h = bbx_h - 1
  end
  if bbx_h ~= #a then eprint(encoding, bbx_h, #a, 'ambiguous bounding box height') end
  if bbx_w > 12 or bbx_h > 12 then eprint(encoding, bbx_w, bbx_h, 'bounding box too large!') end

  -- Pad to 12 rows
  while #a < 12 do table.insert(a, 1, 0x0000) end
  -- Output
  out_buf[#out_buf + 1] = advance_x
  out_buf[#out_buf + 1] = (bbx_x + 256) % 256
  out_buf[#out_buf + 1] = (bbx_y + 256) % 256
  for i = 1, 12, 2 do
    out_buf[#out_buf + 1] = (a[i] >> 8)
    out_buf[#out_buf + 1] = (a[i] & 0xf0) | (a[i + 1] >> 12)
    out_buf[#out_buf + 1] = ((a[i + 1] >> 4) & 0xff)
  end
  glyph_offs[encoding] = n_glyphs
  n_glyphs = n_glyphs + 1

--[[
  if encoding == 22812 then
    eprint('glyph', encoding)
    for i = 1, #a do eprint(string.format('0x%04x', a[i])) end
    for i = 20, 0, -1 do
      eprint(string.format('0x%02x', out_buf[#out_buf - i]))
    end
  end
]]
end

eprint('n_glyphs', n_glyphs)
eprint('index size', 65536 * 2)
eprint('bitmap size', n_glyphs * 21)
for i = 0, 65535 do
  if glyph_offs[i] == -1 then
    io.output():write(string.char(0xff))
    io.output():write(string.char(0xff))
  else
    io.output():write(string.char(glyph_offs[i] >> 8))
    io.output():write(string.char(glyph_offs[i] & 0xff))
  end
end
io.output():write(string.char(table.unpack(out_buf)))
-- for i = 1, #out_buf do print(string.format('0x%02x', out_buf[i])) end

-- 1234⇡1ijm夜)&
