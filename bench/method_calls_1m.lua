local Counter = {}
Counter.__index = Counter
function Counter.new() return setmetatable({n = 0}, Counter) end
function Counter:inc() self.n = self.n + 1 end
local c = Counter.new()
for i = 1, 1000000 do c:inc() end
print(c.n)
