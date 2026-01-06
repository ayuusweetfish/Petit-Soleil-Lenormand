local s = io.open('Tamzen7x14r.bdf', 'r'):read('a')

local allbitmap = {}

for encoding, bitmap in s:gmatch('STARTCHAR%s[^\n]-\nENCODING([^\n]+).-BITMAP.-([0-9A-Fa-f\n]+)ENDCHAR\n') do
  encoding = tonumber(encoding)
  if encoding >= 32 and encoding <= 126 then
    local a = {}
    a[#a + 1] = 0   -- 14 rows to 16 rows
    for i in bitmap:gmatch('[0-9A-Fa-f]+') do
      a[#a + 1] = tonumber(i, 16)
    end
    a[#a + 1] = 0
    allbitmap[encoding] = a
  end
end

for ch = 32, 126 do
  local s = {}
  for i = 1, 14 do
    s[i] = string.format('0x%02x', allbitmap[ch][i])
  end
  print('  {' .. table.concat(s, ', ') .. '},')
end
