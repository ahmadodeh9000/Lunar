local sum = 0
local i = 0
while i < 10000000 do
  sum = sum + i
  i = i + 1
end
print(string.format("%d", sum))
