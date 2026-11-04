import math

class Body:
    def __init__(self, x, y, z, vx, vy, vz, mass):
        self.x = x; self.y = y; self.z = z
        self.vx = vx; self.vy = vy; self.vz = vz
        self.mass = mass

def energy(bodies):
    e = 0.0
    n = len(bodies)
    for i in range(n):
        b = bodies[i]
        e += 0.5 * b.mass * (b.vx * b.vx + b.vy * b.vy + b.vz * b.vz)
        for j in range(i + 1, n):
            c = bodies[j]
            dx = b.x - c.x; dy = b.y - c.y; dz = b.z - c.z
            e -= b.mass * c.mass / math.sqrt(dx * dx + dy * dy + dz * dz)
    return e

def advance(bodies, dt):
    n = len(bodies)
    for i in range(n):
        b = bodies[i]
        for j in range(i + 1, n):
            c = bodies[j]
            dx = b.x - c.x; dy = b.y - c.y; dz = b.z - c.z
            d2 = dx * dx + dy * dy + dz * dz
            mag = dt / (d2 * math.sqrt(d2))
            bm = b.mass * mag; cm = c.mass * mag
            b.vx -= dx * cm; b.vy -= dy * cm; b.vz -= dz * cm
            c.vx += dx * bm; c.vy += dy * bm; c.vz += dz * bm
    for b in bodies:
        b.x += dt * b.vx; b.y += dt * b.vy; b.z += dt * b.vz

def main():
    bodies = [
        Body(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 39.47841760435743),
        Body(4.841431442464721, -1.1603200440274284, -0.10362204447112311, 0.606326392995832, 2.81198684491626, -0.02521836165988763, 0.03769367487038949),
        Body(8.34336671824458, 4.124798564124305, -0.4035234171143214, -1.0107743461787924, 1.8256623712304119, 0.008415761376584154, 0.011286326131968767),
        Body(12.894369562139131, -15.111151401698631, -0.22330757889265573, 1.0827910064415354, 0.8687130181696082, -0.010832637401363636, 0.0017237240570597112),
        Body(15.379697114850917, -25.919314609987964, 0.17925877295037118, 0.979090732243898, 0.5946989986476762, -0.034755955504078104, 0.0020336868699246304)
    ]
    px = py = pz = 0.0
    for b in bodies:
        px += b.vx * b.mass; py += b.vy * b.mass; pz += b.vz * b.mass
    sun = bodies[0]
    sun.vx = -px / sun.mass; sun.vy = -py / sun.mass; sun.vz = -pz / sun.mass
    for s in range(100000):
        advance(bodies, 0.01)
    return energy(bodies)

print(main())
