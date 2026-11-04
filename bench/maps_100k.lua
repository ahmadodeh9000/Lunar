local function main()
  local n = 100000
  local keys = {}
  for i = 0, n - 1 do keys[i] = "k" .. i end
  local m, count = {}, 0
  for i = 0, n - 1 do m[keys[i]] = i; count = count + 1 end
  local total = 0
  for r = 1, 5 do
    for i = 0, n - 1 do total = total + m[keys[i]] end
  end
  for i = 0, n - 1, 2 do m[keys[i]] = nil; count = count - 1 end
  local ints = {}
  for i = 0, n - 1 do ints[i] = i * 2 end
  for i = 0, n - 1 do total = total + ints[i] end
  return total + count
end
print(main())
