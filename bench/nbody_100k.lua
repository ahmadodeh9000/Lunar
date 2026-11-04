local function energy(bodies)
  local e, n = 0, #bodies
  for i = 1, n do
    local b = bodies[i]
    e = e + 0.5 * b.mass * (b.vx * b.vx + b.vy * b.vy + b.vz * b.vz)
    for j = i + 1, n do
      local c = bodies[j]
      local dx, dy, dz = b.x - c.x, b.y - c.y, b.z - c.z
      e = e - b.mass * c.mass / math.sqrt(dx * dx + dy * dy + dz * dz)
    end
  end
  return e
end

local function advance(bodies, dt)
  local n = #bodies
  for i = 1, n do
    local b = bodies[i]
    for j = i + 1, n do
      local c = bodies[j]
      local dx, dy, dz = b.x - c.x, b.y - c.y, b.z - c.z
      local d2 = dx * dx + dy * dy + dz * dz
      local mag = dt / (d2 * math.sqrt(d2))
      local bm, cm = b.mass * mag, c.mass * mag
      b.vx = b.vx - dx * cm; b.vy = b.vy - dy * cm; b.vz = b.vz - dz * cm
      c.vx = c.vx + dx * bm; c.vy = c.vy + dy * bm; c.vz = c.vz + dz * bm
    end
  end
  for i = 1, n do
    local b = bodies[i]
    b.x = b.x + dt * b.vx; b.y = b.y + dt * b.vy; b.z = b.z + dt * b.vz
  end
end

local function Body(x, y, z, vx, vy, vz, mass)
  return {x = x, y = y, z = z, vx = vx, vy = vy, vz = vz, mass = mass}
end

local function main()
  local bodies = {
    Body(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 39.47841760435743),
    Body(4.841431442464721, -1.1603200440274284, -0.10362204447112311, 0.606326392995832, 2.81198684491626, -0.02521836165988763, 0.03769367487038949),
    Body(8.34336671824458, 4.124798564124305, -0.4035234171143214, -1.0107743461787924, 1.8256623712304119, 0.008415761376584154, 0.011286326131968767),
    Body(12.894369562139131, -15.111151401698631, -0.22330757889265573, 1.0827910064415354, 0.8687130181696082, -0.010832637401363636, 0.0017237240570597112),
    Body(15.379697114850917, -25.919314609987964, 0.17925877295037118, 0.979090732243898, 0.5946989986476762, -0.034755955504078104, 0.0020336868699246304)
  }
  local px, py, pz = 0, 0, 0
  for _, b in ipairs(bodies) do px = px + b.vx * b.mass; py = py + b.vy * b.mass; pz = pz + b.vz * b.mass end
  local sun = bodies[1]
  sun.vx = -px / sun.mass; sun.vy = -py / sun.mass; sun.vz = -pz / sun.mass
  for s = 1, 100000 do advance(bodies, 0.01) end
  return energy(bodies)
end
print(main())
