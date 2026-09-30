local function make()
  local n = 0
  return function() n = n + 1; return n end
end
local f = make()
local r = 0
for i = 1, 1000000 do r = f() end
print(r)
