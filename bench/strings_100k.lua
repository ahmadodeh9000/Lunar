local function main()
  local total = 0
  for i = 0, 99999 do
    local line = "item " .. i .. ": x=" .. (i * 2) .. " y=" .. (i + 1)
    local parts = {}
    for word in string.gmatch(line, "[^ ]+") do parts[#parts + 1] = word end
    local back = table.concat(parts, ",")
    total = total + #back + #parts
  end
  return total
end
print(main())
