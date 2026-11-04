local function main()
  local a = {}
  for i = 0, 999999 do a[#a + 1] = i end
  local total = 0
  for r = 1, 10 do
    for _, x in ipairs(a) do total = total + x end
  end
  local m = {}
  for i = 0, 999 do m["k" .. i] = i end
  for r = 1, 1000 do
    for k, v in pairs(m) do total = total + v end
  end
  return total
end
print(main())
