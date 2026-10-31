# LunarCraft

A small Minecraft-style game written in [Lunar](../../README.md). This page explains how it works, then goes through [`minecraft.lunar`](minecraft.lunar) line by line.

Build with `make sdl`, then run it from the repo root:

```bash
./lunar examples/minecraft/minecraft.lunar
```

| Input | Action |
|---|---|
| Mouse | Look |
| `W` `A` `S` `D` | Move |
| `Space` | Jump |
| Left click | Break the targeted block |
| Right click | Place the selected block |
| `1`-`8` | Select a block type |
| `Esc` | Quit |

To test the game logic without opening a window:

```bash
sh examples/minecraft/run_tests.sh
```

## What's in this folder

| File | Purpose |
|---|---|
| `minecraft.lunar` | The game |
| `atlas.png` | The block textures |
| `make_atlas.py` | Generates `atlas.png` |
| `game_tests.lunar` | Tests for physics, raycasting, and block editing |
| `run_tests.sh` | Runs those tests |

## How a frame works

Each pass through the main loop does this:

1. **Input and physics:** read the mouse and keys, then move the player.
2. **Camera and raycast:** recalculate the view, then find the block under the crosshair.
3. **Edits:** break or place a block, rebuilding only the affected chunks.
4. **Project:** turn each visible face into a quad on the screen.
5. **Sort:** put the quads in back-to-front order.
6. **Draw:** send everything to SDL in one call, then draw the HUD.

## Ideas to know before reading

**Coordinates.** `y` points up. The player and blocks use the same units, where one block is 1 x 1 x 1. Block `(x, y, z)` fills the space from `x` to `x + 1`, and likewise on the other axes.

**The world is one flat array.** Lunar has no 3D arrays or hash maps, so a block's position in `world` is `x + z*SX + y*SXZ`.

**Only visible faces are stored.** A face hidden by a neighboring block is never drawn. The list of remaining faces is built per chunk (8 x 8 columns) and changes only when a block changes.

**Camera space.** To draw a point, the game takes its offset from the camera and measures it along the camera's right, up, and forward directions. Forward distance is depth, and dividing by depth makes far things smaller.

**Painter's algorithm.** There is no depth buffer, so far things are drawn first and near things paint over them. The sort key is the grid distance between a block's cell and the camera's cell.

**Quads.** Everything drawn is a quad of 4 vertices, and each vertex is 5 numbers: `x, y, u, v, color`. `u` and `v` say which part of `atlas.png` to show.

**Lunar habits you'll see:**

- `let x = SOMETHING;` at the top of a hot function makes a local copy, because locals are the fastest variables.
- `ret` inside a loop leaves the whole function, which stands in for the missing `break`.
- Bitwise math always has parentheses, because Lunar's operator precedence differs from C.
- A function can only use names defined above it, because everything is a local of `game()`.

## The code, line by line

Blank lines are skipped. Line numbers match `minecraft.lunar`.

### File header

| Line | Code | What it does |
|---:|---|---|
| 1 | `` // A small Minecraft-style voxel game. Run from the repo root, on a `make sdl` build: `` | Says what this file is and that it needs an SDL build. Running it from this folder also works, because `load_atlas` tries both paths. |
| 2 | `//     ./lunar examples/minecraft/minecraft.lunar` | The command to run the game from the repo root. |
| 3 | `//` | Blank comment line, used as a spacer. |
| 4 | `// Everything here is written to avoid allocating per frame: the vertex and index` | Performance rule for the file: don't create new objects every frame, because that wakes up the garbage collector. |
| 5 | `// buffers are preallocated and overwritten, and hot loops copy globals to locals.` | Explains how: buffers are made once and reused, and hot loops copy shared variables into locals. Those variables were globals when this was written; they are now upvalues of `game()`, as line 10 explains. |
| 6 | `//` | Blank comment line. |
| 7 | `// Lunar's bitwise operators bind tighter than arithmetic, and \| binds tighter than` | Warns that Lunar's bitwise operators bind tighter than `+` and `*`. |
| 8 | `// & , the reverse of C, so bitwise expressions are always fully parenthesized.` | And that `\|` binds tighter than `&`, the opposite of C, so this file always adds parentheses around bitwise math. |
| 9 | `//` | Blank comment line. |
| 10 | `// The whole game lives inside game() because a chunk holds at most 256 constants, and` | Why everything sits inside one function: a chunk (compiled unit of code) can hold only 256 constants. |
| 11 | `// at the top level every global's name and every function takes some. As locals of` | At the top level, global names and functions all use up those constants. |
| 12 | `// game() they take none, and functions reach them as upvalues, which are also faster` | Inside `game()` they are local variables, which use none. Inner functions reach them as upvalues, and those are faster than global lookups. |
| 13 | `// than globals. The catch: a function can only use what is defined above it.` | The downside: a function can use only names defined above it. Order matters throughout this file. |

### Start of the game and settings

| Line | Code | What it does |
|---:|---|---|
| 15 | `fn game() {` | Starts `game()`. All of the game's state and functions live inside it, and it is called on the last line of the file. |
| 16 | `let WIN_W = 960;` | Window width in pixels. |
| 17 | `let WIN_H = 600;` | Window height in pixels. |
| 18 | `let FOV_DEGREES = 70;` | Vertical field of view, in degrees. |
| 19 | `let NEAR = 0.05;` | Near plane: geometry closer to the camera than this is cut off, which avoids dividing by zero or a tiny number when projecting. |
| 20 | `let RENDER_RADIUS = 26;` | Blocks farther than this, measured horizontally, are not drawn. |
| 21 | `let SUBDIVIDE_DIST = 4;     // faces closer than this are split, to hide affine texture warp` | Faces with a corner closer than this are split into smaller pieces. SDL maps textures without perspective correction, so big nearby quads would look warped. |
| 22 | `let SUBDIVISIONS = 4;` | Each near face is split into 4 x 4 pieces. |
| 23 | `let MAX_QUADS = 9000;` | Most quads allowed per frame. Sets the size of the preallocated buffers. |
| 25 | `// Set above 0 to spin in place for that many seconds with vsync off, then exit.` | Explains the setting below. |
| 26 | `let DEMO_SECONDS = 0;` | Benchmark switch. 0 means normal play. Any higher number spins the camera for that many seconds with vsync off, then quits. |

### World: sizes and block types

| Line | Code | What it does |
|---:|---|---|
| 28 | `// ---------- world ----------` | Section marker. |
| 30 | `let SX = 32;` | World width (x axis), in blocks. |
| 31 | `let SY = 16;` | World height (y axis, which points up), in blocks. |
| 32 | `let SZ = 32;` | World depth (z axis), in blocks. |
| 33 | `let SXZ = SX * SZ;` | Blocks in one horizontal layer (32 x 32 = 1024). Used to step up one layer in the flat `world` array. |
| 34 | `let CHUNK = 8;` | A chunk is 8 x 8 columns. Chunks let the game cull and rebuild the world in pieces. |
| 35 | `let CHUNKS_X = SX / CHUNK;` | Chunks along x: 4. |
| 36 | `let CHUNKS_Z = SZ / CHUNK;` | Chunks along z: 4, so 16 in total. |
| 38 | `let AIR = 0;` | Block type 0: empty space. |
| 39 | `let GRASS = 1;` | Block type 1. |
| 40 | `let DIRT = 2;` | Block type 2. |
| 41 | `let STONE = 3;` | Block type 3. |
| 42 | `let LOG = 4;` | Block type 4: tree trunk. |
| 43 | `let LEAVES = 5;` | Block type 5. |
| 44 | `let SAND = 6;` | Block type 6. |
| 45 | `let PLANKS = 7;` | Block type 7. |
| 46 | `let BRICK = 8;` | Block type 8. |
| 48 | `// atlas tile of each block type, indexed by block` | Explains the three tables below. Slot 0 is air, which is never drawn. |
| 49 | `let TILE_TOP  = [0, 0, 2, 3, 5, 6, 7, 8, 9];` | Atlas tile for each block's top face. Grass (slot 1) uses tile 0, the green top. |
| 50 | `let TILE_SIDE = [0, 1, 2, 3, 4, 6, 7, 8, 9];` | Atlas tile for side faces. Grass uses tile 1, green over dirt. |
| 51 | `let TILE_BOT  = [0, 2, 2, 3, 5, 6, 7, 8, 9];` | Atlas tile for bottom faces. Grass uses tile 2, plain dirt. |
| 52 | `let TILE_HIGHLIGHT = 14;` | Atlas tile holding the white outline used to highlight the targeted block. |
| 54 | `let world = [];` | All the blocks, as one flat array. A block's position is `x + z*SX + y*SXZ`. |
| 55 | `let chunk_faces = [];` | One face list per chunk, filled by `build_chunk`. |

### World: noise

| Line | Code | What it does |
|---:|---|---|
| 57 | `// Values stay under 2^31 at every step: an out-of-range double saturates or wraps` | Why the hash below keeps its numbers small: Lunar numbers are doubles, and bitwise operators convert them to 32-bit integers. |
| 58 | `// depending on the platform when bitwise operators truncate it.` | A number too big for that conversion gives different results on different CPUs. |
| 59 | `fn hash2(x, z, seed) {` | Turns a grid position and a seed into a repeatable pseudo-random number from 0 to 1. |
| 60 | `let h = ((x * 3571) + (z * 6151) + seed) & 65535;` | Mixes the position and seed into a starting value, kept to 16 bits by `& 65535`. |
| 61 | `h = ((h * 25173) + 13849) & 65535;` | Scrambles it with a multiply-and-add step, again kept to 16 bits. 65535 x 25173 still fits in 32 bits. |
| 62 | `h = h ^ (h >> 7);` | Folds the high bits down into the low bits, which are the weakest after that step. |
| 63 | `h = ((h * 25173) + 13849) & 65535;` | A second scramble. |
| 64 | `ret h / 65535;` | Divides by the maximum value to get a number from 0 to 1. |
| 65 | `}` | Ends `hash2` (line 59). |
| 67 | `fn value_noise(x, z, scale, seed) {` | Smooth random surface: random heights at grid points, blended in between. `scale` is the distance between grid points, so bigger means broader hills. |
| 68 | `let fx = x / scale;` | Position in grid units, x. |
| 69 | `let fz = z / scale;` | Position in grid units, z. |
| 70 | `let ix = floor(fx);` | Grid cell containing the point, x. |
| 71 | `let iz = floor(fz);` | Grid cell containing the point, z. |
| 72 | `let tx = fx - ix;` | How far across the cell the point is, x (0 to 1). |
| 73 | `let tz = fz - iz;` | How far across the cell the point is, z (0 to 1). |
| 74 | `tx = tx * tx * (3 - 2 * tx);` | Smoothstep: eases the blend so there are no visible creases at cell edges, x. |
| 75 | `tz = tz * tz * (3 - 2 * tz);` | Smoothstep, z. |
| 76 | `let a = hash2(ix, iz, seed);` | Random height at the cell's corner (ix, iz). |
| 77 | `let b = hash2(ix + 1, iz, seed);` | At corner (ix+1, iz). |
| 78 | `let c = hash2(ix, iz + 1, seed);` | At corner (ix, iz+1). |
| 79 | `let d = hash2(ix + 1, iz + 1, seed);` | At corner (ix+1, iz+1). |
| 80 | `let top = a + (b - a) * tx;` | Blends the two corners on one edge along x. |
| 81 | `let bot = c + (d - c) * tx;` | Blends the two corners on the opposite edge along x. |
| 82 | `ret top + (bot - top) * tz;` | Blends those two results along z. This is the final value, from 0 to 1. |
| 83 | `}` | Ends `value_noise` (line 67). |

### World: terrain height and block access

| Line | Code | What it does |
|---:|---|---|
| 85 | `fn terrain_height(x, z) {` | Gives the height of the ground at a column. |
| 86 | `let h = 2 + 6 * value_noise(x, z, 9, 11) + 3 * value_noise(x, z, 4, 97);` | Adds broad hills (scale 9) and smaller bumps (scale 4) to a base height of 2. |
| 87 | `ret min(9, max(2, floor(h)));` | Rounds down and limits the result to 2-9, which leaves headroom for trees in the 16-tall world. |
| 88 | `}` | Ends `terrain_height` (line 85). |
| 90 | `fn set_block(x, y, z, b) { world[x + z * SX + y * SXZ] = b; }` | Stores block `b` at a position, using the flat-array index. Does no bounds check, so callers must pass a valid position. |
| 92 | `fn in_world(x, y, z) {` | Checks whether a position is inside the world. |
| 93 | `ret x >= 0 && x < SX && y >= 0 && y < SY && z >= 0 && z < SZ;` | True only if all three coordinates are within bounds. `&&` stops at the first failure. |
| 94 | `}` | Ends `in_world` (line 92). |
| 96 | `fn get_block(x, y, z) {` | Reads a block safely. |
| 97 | `if (!in_world(x, y, z)) ret AIR;` | Anything outside the world counts as air. |
| 98 | `ret world[x + z * SX + y * SXZ];` | Otherwise returns the stored block. |
| 99 | `}` | Ends `get_block` (line 96). |

### World: trees

| Line | Code | What it does |
|---:|---|---|
| 101 | `fn place_leaf(x, y, z) {` | Places one leaf block. |
| 102 | `if (in_world(x, y, z) && get_block(x, y, z) == AIR) set_block(x, y, z, LEAVES);` | Only if it is inside the world and empty, so leaves never replace terrain or go out of bounds. |
| 103 | `}` | Ends `place_leaf` (line 101). |
| 105 | `fn place_leaf_square(cx, y, cz, r) {` | Places a flat square of leaves centered on (cx, cz), reaching `r` blocks in each direction. |
| 106 | `for (let dz = -r; dz <= r; dz = dz + 1) {` | Loops across z, from -r to r. |
| 107 | `for (let dx = -r; dx <= r; dx = dx + 1) {` | Loops across x, from -r to r. |
| 108 | `place_leaf(cx + dx, y, cz + dz);` | Places one leaf at this offset. |
| 109 | `}` | Ends the `for` loop from line 107. |
| 110 | `}` | Ends the `for` loop from line 106. |
| 111 | `}` | Ends `place_leaf_square` (line 105). |
| 113 | `fn place_tree(x, ground, z) {` | Builds one tree. `ground` is the height of the block it stands on. |
| 114 | `let top = ground + 4;` | The trunk's top is 4 blocks above the ground. |
| 115 | `place_leaf_square(x, top - 1, z, 2);` | Wide leaf layer (5 x 5) just under the trunk top. |
| 116 | `place_leaf_square(x, top, z, 2);` | Wide leaf layer (5 x 5) level with the trunk top. |
| 117 | `place_leaf_square(x, top + 1, z, 1);` | Narrower layer (3 x 3) above it. |
| 118 | `place_leaf(x, top + 2, z);` | A single leaf on the very top. |
| 119 | `for (let y = ground + 1; y <= top; y = y + 1) set_block(x, y, z, LOG);` | Fills the trunk from just above the ground to its top. Done last so logs overwrite the leaves in the middle. |
| 120 | `}` | Ends `place_tree` (line 113). |
| 122 | `// One candidate tree per cell of a coarse grid, so trees spread out instead of clumping.` | Why trees are placed this way: the simple hash clumps trees when used per column. |
| 123 | `fn place_trees() {` | Scatters trees across the world. |
| 124 | `let cell = 7;` | Each grid cell is 7 x 7 and gets at most one tree. |
| 125 | `for (let cz = 0; cz + cell <= SZ; cz = cz + cell) {` | Loops over cells along z, stopping before a cell would pass the world edge. |
| 126 | `for (let cx = 0; cx + cell <= SX; cx = cx + cell) {` | Loops over cells along x, the same way. |
| 127 | `let x = cx + 2 + floor(hash2(cx, cz, 501) * (cell - 3));` | Picks a random x inside the cell, keeping 2 blocks from its edge so leaves stay close to the cell. |
| 128 | `let z = cz + 2 + floor(hash2(cz, cx, 907) * (cell - 3));` | Picks a random z the same way, with a different seed. |
| 129 | `let h = terrain_height(x, z);` | Finds the ground height there. |
| 130 | `if (h > 3 && hash2(cx + 3, cz + 5, 4242) > 0.3) place_tree(x, h, z);` | Plants a tree if the ground isn't low sand (height above 3) and a random check passes, which about 70% of cells do. |
| 131 | `}` | Ends the `for` loop from line 126. |
| 132 | `}` | Ends the `for` loop from line 125. |
| 133 | `}` | Ends `place_trees` (line 123). |

### World: generation

| Line | Code | What it does |
|---:|---|---|
| 135 | `fn generate_world() {` | Creates the whole world. |
| 136 | `for (let i = 0; i < SXZ * SY; i = i + 1) push(world, AIR);` | Fills the array with air. `push` is the only way to grow an array, so this sizes it too. |
| 137 | `for (let z = 0; z < SZ; z = z + 1) {` | Loops over all columns along z. |
| 138 | `for (let x = 0; x < SX; x = x + 1) {` | And along x. |
| 139 | `let h = terrain_height(x, z);` | Gets this column's ground height. |
| 140 | `for (let y = 0; y <= h; y = y + 1) {` | Loops from the bottom up to the ground surface. |
| 141 | `let b = STONE;` | Assumes stone by default. |
| 142 | `if (y == h) { if (h <= 3) b = SAND; else b = GRASS; }` | The surface block: sand on low ground (height 3 or less), otherwise grass. |
| 143 | `else if (y >= h - 2) { if (h <= 3) b = SAND; else b = DIRT; }` | The two blocks under the surface: sand on low ground, otherwise dirt. |
| 144 | `set_block(x, y, z, b);` | Stores the chosen block. |
| 145 | `}` | Ends the `for` loop from line 140. |
| 146 | `}` | Ends the `for` loop from line 138. |
| 147 | `}` | Ends the `for` loop from line 137. |
| 148 | `place_trees();` | Adds trees once the terrain exists. |
| 149 | `}` | Ends `generate_world` (line 135). |

### Face list

| Line | Code | What it does |
|---:|---|---|
| 151 | `// ---------- face list ----------` | Section marker. |
| 153 | `// A chunk's faces are a flat array of records: x, y, z, dir, tile, plane. Directions` | Each visible face is stored as 6 numbers in a row: position, direction, atlas tile, and a plane value used for a fast back-face test. |
| 154 | `// are +X -X +Y -Y +Z -Z. A face is front-facing when CAM_PLANE[dir] > plane.` | Direction numbers 0-5 mean +X, -X, +Y, -Y, +Z, -Z. Comparing the camera's position with the plane tells whether a face points toward the camera. |
| 155 | `let FACE_STRIDE = 6;` | The number of values per face record. |
| 157 | `// Is this neighbor hiding a face? Outside the sides and top of the world counts as` | Explains the function below: a face only needs drawing if the block next to it doesn't hide it. |
| 158 | `// open, so the world's edges have walls; below it counts as solid, so no underside.` | The world's sides and top count as open, so the edges of the world are drawn. Below the world counts as solid, so the underside isn't. |
| 159 | `fn hides_face(x, y, z) {` | Returns true if the block at this position hides the face touching it. |
| 160 | `if (y < 0) ret true;` | Below the world: hidden. |
| 161 | `if (y >= SY \|\| x < 0 \|\| x >= SX \|\| z < 0 \|\| z >= SZ) ret false;` | Above or beside the world: not hidden. |
| 162 | `ret world[x + z * SX + y * SXZ] != AIR;` | Inside the world: hidden by any block that isn't air. |
| 163 | `}` | Ends `hides_face` (line 159). |
| 165 | `fn add_face(out, x, y, z, dir, tile, plane) {` | Adds one face record to a list. |
| 166 | `push(out, x); push(out, y); push(out, z); push(out, dir); push(out, tile); push(out, plane);` | Appends the 6 values of the record, in order. |
| 167 | `}` | Ends `add_face` (line 165). |
| 169 | `fn build_chunk(ci) {` | Rebuilds the face list of chunk number `ci`. |
| 170 | `let out = [];` | Starts a new, empty list. |
| 171 | `let x0 = (ci % CHUNKS_X) * CHUNK;` | The chunk's starting x: its column number (`ci % 4`) times 8. |
| 172 | `let z0 = floor(ci / CHUNKS_X) * CHUNK;` | The chunk's starting z: its row number times 8. |
| 173 | `for (let y = 0; y < SY; y = y + 1) {` | Loops over every height. |
| 174 | `for (let z = z0; z < z0 + CHUNK; z = z + 1) {` | Loops over the chunk's z range. |
| 175 | `for (let x = x0; x < x0 + CHUNK; x = x + 1) {` | Loops over the chunk's x range. |
| 176 | `let b = world[x + z * SX + y * SXZ];` | Reads the block here. |
| 177 | `if (b != AIR) {` | Air has no faces, so only solid blocks continue. |
| 178 | `let side = TILE_SIDE[b];` | Looks up the side tile once, since four faces use it. |
| 179 | `if (!hides_face(x + 1, y, z)) add_face(out, x, y, z, 0, side, x + 1);` | +X face, if open. Its plane value is `x + 1`, and it faces the camera when the camera's x is greater than that. |
| 180 | `if (!hides_face(x - 1, y, z)) add_face(out, x, y, z, 1, side, -x);` | -X face. Plane values are negated for negative directions, so one "greater than" test works for all six. |
| 181 | `if (!hides_face(x, y + 1, z)) add_face(out, x, y, z, 2, TILE_TOP[b], y + 1);` | +Y (top) face, using the top tile. |
| 182 | `if (!hides_face(x, y - 1, z)) add_face(out, x, y, z, 3, TILE_BOT[b], -y);` | -Y (bottom) face, using the bottom tile. |
| 183 | `if (!hides_face(x, y, z + 1)) add_face(out, x, y, z, 4, side, z + 1);` | +Z face. |
| 184 | `if (!hides_face(x, y, z - 1)) add_face(out, x, y, z, 5, side, -z);` | -Z face. |
| 185 | `}` | Ends the `if` block from line 177. |
| 186 | `}` | Ends the `for` loop from line 175. |
| 187 | `}` | Ends the `for` loop from line 174. |
| 188 | `}` | Ends the `for` loop from line 173. |
| 189 | `chunk_faces[ci] = out;` | Replaces the chunk's old list with the new one. |
| 190 | `}` | Ends `build_chunk` (line 169). |
| 192 | `fn build_all_chunks() {` | Builds every chunk at startup. |
| 193 | `for (let i = 0; i < CHUNKS_X * CHUNKS_Z; i = i + 1) {` | Loops over all 16 chunks. |
| 194 | `push(chunk_faces, nil);` | Adds a slot for this chunk. Setting an index can't grow a Lunar array, so the slot has to exist first. |
| 195 | `build_chunk(i);` | Fills that slot. |
| 196 | `}` | Ends the `for` loop from line 193. |
| 197 | `}` | Ends `build_all_chunks` (line 192). |

### Camera

| Line | Code | What it does |
|---:|---|---|
| 199 | `// ---------- camera ----------` | Section marker. |
| 201 | `let EYE_HEIGHT = 1.62;` | How high the eyes are above the player's feet, in blocks. |
| 202 | `let eye_x = 0;` | Camera x. |
| 203 | `let eye_y = 0;` | Camera y. |
| 204 | `let eye_z = 0;` | Camera z. |
| 205 | `let yaw = 2.35;` | Left-right view angle, in radians. Angle 0 faces -z and larger angles turn right. |
| 206 | `let pitch = -0.2;` | Up-down view angle, in radians. Negative looks down. |
| 208 | `let FOCAL = (WIN_H / 2) / tan(FOV_DEGREES * 3.14159265 / 360);` | Focal length in pixels, from the field of view: half the window height divided by the tangent of half the angle. Larger means more zoomed in. |
| 209 | `let CX = WIN_W / 2;` | Horizontal center of the window, where the view axis lands. |
| 210 | `let CY = WIN_H / 2;` | Vertical center of the window. |
| 211 | `let CULL_SLOPE = (WIN_W / 2) / FOCAL * 1.05;` | How far sideways the view widens per unit of depth, plus 5% margin. Used for culling. |
| 212 | `let CULL_SLOPE_Y = (WIN_H / 2) / FOCAL * 1.05;` | The same, vertically. |
| 214 | `// Camera basis, in world coordinates: right, up, forward.` | Introduces the camera's three axes, expressed in world coordinates. |
| 215 | `let rgt_x = 1; let rgt_y = 0; let rgt_z = 0;` | Direction to the camera's right. The placeholder values are replaced by `update_camera`. |
| 216 | `let up_x = 0;  let up_y = 1;  let up_z = 0;` | Direction of the camera's up. |
| 217 | `let fwd_x = 0; let fwd_y = 0; let fwd_z = -1;` | Direction the camera faces. |
| 219 | `// Each direction's first corner relative to its block, then the edges along the` | Explains the table below: each row is one face direction, holding 9 numbers. |
| 220 | `// tile's u and v axes.` | Those are its first corner, the edge along the texture's u axis, and the edge along its v axis. |
| 221 | `let FACE_DEFS = [` | Starts the table. Rows are in direction order, +X first. |
| 222 | `1,1,1,  0,0,-1,  0,-1,0,` | +X: starts at the top corner (1,1,1); u runs toward -z and v runs down. |
| 223 | `0,1,0,  0,0,1,   0,-1,0,` | -X: starts at (0,1,0); u runs toward +z and v runs down. |
| 224 | `0,1,0,  1,0,0,   0,0,1,` | +Y (top): starts at (0,1,0); u runs along +x and v along +z. |
| 225 | `0,0,1,  1,0,0,   0,0,-1,` | -Y (bottom): starts at (0,0,1); u runs along +x and v along -z. |
| 226 | `0,1,1,  1,0,0,   0,-1,0,` | +Z: starts at (0,1,1); u runs along +x and v runs down. |
| 227 | `1,1,0,  -1,0,0,  0,-1,0,` | -Z: starts at (1,1,0); u runs along -x and v runs down. |
| 228 | `];` | Ends the table. |
| 229 | `// FACE_DEFS rotated into view space, refreshed each frame.` | Explains `view_defs`. |
| 230 | `let view_defs = [];` | The same table rotated into camera space. Filled by `init_render_buffers` and updated every frame. |
| 231 | `let CAM_PLANE = [0, 0, 0, 0, 0, 0];` | The camera's position as seen by each direction, compared with each face's plane value in the back-face test. |
| 233 | `fn update_camera() {` | Recalculates everything that depends on the view. Called once per frame. |
| 234 | `let sy = sin(yaw);` | Sine of yaw. |
| 235 | `let cy = cos(yaw);` | Cosine of yaw. |
| 236 | `let sp = sin(pitch);` | Sine of pitch. |
| 237 | `let cp = cos(pitch);` | Cosine of pitch. |
| 238 | `rgt_x = cy;      rgt_y = 0;   rgt_z = sy;` | Right vector. It stays horizontal because the camera never rolls. |
| 239 | `up_x = -sy * sp; up_y = cp;   up_z = cy * sp;` | Up vector, tilted by pitch. |
| 240 | `fwd_x = sy * cp; fwd_y = sp;  fwd_z = -cy * cp;` | Forward vector. With both angles at 0 it is (0, 0, -1). |
| 242 | `for (let i = 0; i < 54; i = i + 3) {` | Goes through the 54 table numbers, 3 at a time, as 18 vectors. |
| 243 | `let x = FACE_DEFS[i];` | Vector's x. |
| 244 | `let y = FACE_DEFS[i + 1];` | Vector's y. |
| 245 | `let z = FACE_DEFS[i + 2];` | Vector's z. |
| 246 | `view_defs[i]     = x * rgt_x + y * rgt_y + z * rgt_z;` | Its camera-space x: the dot product with the right vector. |
| 247 | `view_defs[i + 1] = x * up_x + y * up_y + z * up_z;` | Its camera-space y: the dot product with up. |
| 248 | `view_defs[i + 2] = x * fwd_x + y * fwd_y + z * fwd_z;` | Its camera-space z (depth): the dot product with forward. |
| 249 | `}` | Ends the `for` loop from line 242. |
| 250 | `CAM_PLANE[0] = eye_x;  CAM_PLANE[1] = -eye_x;` | Camera position for the +X and -X back-face tests. The -X value is negated to match the negated plane. |
| 251 | `CAM_PLANE[2] = eye_y;  CAM_PLANE[3] = -eye_y;` | The same for +Y and -Y. |
| 252 | `CAM_PLANE[4] = eye_z;  CAM_PLANE[5] = -eye_z;` | The same for +Z and -Z. |
| 253 | `}` | Ends `update_camera` (line 233). |

### Player: settings and state

| Line | Code | What it does |
|---:|---|---|
| 255 | `// ---------- player ----------` | Section marker. |
| 257 | `let PLAYER_HALF_WIDTH = 0.3;` | Half the player's width. The player is a box 0.6 wide. |
| 258 | `let PLAYER_HEIGHT = 1.8;` | The player's height. |
| 259 | `let COLLISION_GAP = 0.001;` | A tiny gap kept between the player and any block, so rounding can't leave the box inside the block. |
| 260 | `let WALK_SPEED = 4.6;` | Walking speed, in blocks per second. |
| 261 | `let JUMP_SPEED = 7.8;` | Upward speed at the start of a jump. High enough to clear one block but not two. |
| 262 | `let GRAVITY = 24;` | Downward acceleration, in blocks per second squared. |
| 263 | `let MAX_FALL = 30;` | Top falling speed. It also keeps a single step from being big enough to pass through a block. |
| 264 | `let MOUSE_SENSITIVITY = 0.0025;` | Radians of turn per pixel of mouse movement. |
| 265 | `let MAX_PITCH = 1.55;` | Limit on looking up or down, just under 90 degrees, so the view can't flip over. |
| 267 | `let px = 0;` | Player's feet, x (center of the box). |
| 268 | `let py = 0;` | Player's feet, y (bottom of the box). |
| 269 | `let pz = 0;` | Player's feet, z (center of the box). |
| 270 | `let vel_y = 0;` | Current vertical speed. Horizontal speed isn't stored, because walking starts and stops instantly. |
| 271 | `let on_ground = false;` | Whether the player is standing on something, which decides if they can jump. |

### Player: collision

| Line | Code | What it does |
|---:|---|---|
| 273 | `// Unlike hides_face, this treats the world's sides as walls so the player stays inside.` | How this differs from `hides_face`. |
| 274 | `fn blocked(x, y, z) {` | Returns true if the player can't occupy this block position. |
| 275 | `if (y < 0) ret true;` | Below the world is solid. |
| 276 | `if (y >= SY) ret false;` | Above the world is open, so jumping on the top layer is safe. |
| 277 | `if (x < 0 \|\| x >= SX \|\| z < 0 \|\| z >= SZ) ret true;` | Beyond the sides is solid: an invisible wall. |
| 278 | `ret world[x + z * SX + y * SXZ] != AIR;` | Otherwise, any block that isn't air. |
| 279 | `}` | Ends `blocked` (line 274). |
| 281 | `fn box_hits_world(x, y, z) {` | Checks if a player box with its feet at (x, y, z) would overlap anything solid. |
| 282 | `let hw = PLAYER_HALF_WIDTH;` | Short name for the half width. |
| 283 | `let x1 = floor(x + hw);` | Last block column the box reaches in x. |
| 284 | `let y1 = floor(y + PLAYER_HEIGHT);` | Last block layer the box reaches in y. |
| 285 | `let z1 = floor(z + hw);` | Last block column the box reaches in z. |
| 286 | `for (let by = floor(y); by <= y1; by = by + 1) {` | Loops over the layers the box touches. |
| 287 | `for (let bz = floor(z - hw); bz <= z1; bz = bz + 1) {` | Loops over its z range. |
| 288 | `for (let bx = floor(x - hw); bx <= x1; bx = bx + 1) {` | Loops over its x range. |
| 289 | `if (blocked(bx, by, bz)) ret true;` | Returns as soon as one touched block is solid. This also serves as Lunar's missing `break`. |
| 290 | `}` | Ends the `for` loop from line 288. |
| 291 | `}` | Ends the `for` loop from line 287. |
| 292 | `}` | Ends the `for` loop from line 286. |
| 293 | `ret false;` | Nothing solid was found. |
| 294 | `}` | Ends `box_hits_world` (line 281). |

### Player: moving one axis at a time

| Line | Code | What it does |
|---:|---|---|
| 296 | `fn move_axis_x(dx) {` | Moves the player along x by `dx`. |
| 297 | `if (dx == 0) ret;` | Nothing to do if there is no movement. |
| 298 | `let nx = px + dx;` | The position it wants to reach. |
| 299 | `if (box_hits_world(nx, py, pz)) {` | If that spot is blocked: |
| 300 | `if (dx > 0) nx = floor(nx + PLAYER_HALF_WIDTH) - PLAYER_HALF_WIDTH - COLLISION_GAP;` | Moving +x: stops just before the block's left side. |
| 301 | `else nx = floor(nx - PLAYER_HALF_WIDTH) + 1 + PLAYER_HALF_WIDTH + COLLISION_GAP;` | Moving -x: stops just after the block's right side. |
| 302 | `if (box_hits_world(nx, py, pz)) nx = px;` | If even that spot is blocked, stays where it was. |
| 303 | `}` | Ends the `if` block from line 299. |
| 304 | `px = nx;` | Commits the new position. |
| 305 | `}` | Ends `move_axis_x` (line 296). |
| 307 | `fn move_axis_z(dz) {` | Moves the player along z. Works the same as `move_axis_x`. |
| 308 | `if (dz == 0) ret;` | Nothing to do if there is no movement. |
| 309 | `let nz = pz + dz;` | The position it wants to reach. |
| 310 | `if (box_hits_world(px, py, nz)) {` | If that spot is blocked: |
| 311 | `if (dz > 0) nz = floor(nz + PLAYER_HALF_WIDTH) - PLAYER_HALF_WIDTH - COLLISION_GAP;` | Moving +z: stops just before the block. |
| 312 | `else nz = floor(nz - PLAYER_HALF_WIDTH) + 1 + PLAYER_HALF_WIDTH + COLLISION_GAP;` | Moving -z: stops just after the block. |
| 313 | `if (box_hits_world(px, py, nz)) nz = pz;` | If even that spot is blocked, stays where it was. |
| 314 | `}` | Ends the `if` block from line 310. |
| 315 | `pz = nz;` | Commits the new position. |
| 316 | `}` | Ends `move_axis_z` (line 307). |
| 318 | `fn move_axis_y(dy) {` | Moves the player vertically, which also decides whether they are standing on something. |
| 319 | `on_ground = false;` | Assumes they are in the air until a landing proves otherwise. |
| 320 | `let ny = py + dy;` | The height it wants to reach. |
| 321 | `if (box_hits_world(px, ny, pz)) {` | If that spot is blocked: |
| 322 | `if (dy < 0) {` | Moving down, so this is a landing: |
| 323 | `ny = floor(ny) + 1 + COLLISION_GAP;` | Puts the feet just above the block top. |
| 324 | `on_ground = true;` | Records that the player is on the ground. |
| 325 | `} else {` | Moving up, so the head hit something: |
| 326 | `ny = floor(ny + PLAYER_HEIGHT) - PLAYER_HEIGHT - COLLISION_GAP;` | Puts the head just below the block. |
| 327 | `}` | Ends the `else` branch from line 325. |
| 328 | `vel_y = 0;` | Either way, vertical movement stops. |
| 329 | `if (box_hits_world(px, ny, pz)) ny = py;` | If even that spot is blocked, stays where it was. |
| 330 | `}` | Ends the `if` block from line 321. |
| 331 | `py = ny;` | Commits the new height. |
| 332 | `}` | Ends `move_axis_y` (line 318). |

### Player: each frame

| Line | Code | What it does |
|---:|---|---|
| 334 | `// forward and strafe are -1, 0 or 1. Each axis resolves on its own, which lets the` | Explains the inputs: each is -1, 0 or 1. |
| 335 | `// player slide along walls.` | Moving one axis at a time is what makes sliding along walls work: the blocked axis stops and the other continues. |
| 336 | `fn step_player(dt, forward, strafe, jump) {` | Advances the player by `dt` seconds. |
| 337 | `let sy = sin(yaw);` | Sine of the view angle. |
| 338 | `let cy = cos(yaw);` | Cosine of the view angle. |
| 339 | `let wx = sy * forward + cy * strafe;` | Desired x direction: forward's x part plus strafe's x part. Pitch is ignored, so looking up doesn't slow walking. |
| 340 | `let wz = -cy * forward + sy * strafe;` | Desired z direction, the same way. |
| 341 | `let wish = sqrt(wx * wx + wz * wz);` | Length of that direction. |
| 342 | `if (wish > 0) {` | If the player is trying to move at all: |
| 343 | `wx = wx / wish * WALK_SPEED;` | Scales x so that diagonals aren't faster than straight lines. |
| 344 | `wz = wz / wish * WALK_SPEED;` | Scales z the same way. |
| 345 | `}` | Ends the `if` block from line 342. |
| 346 | `if (jump && on_ground) vel_y = JUMP_SPEED;` | Starts a jump, only when standing on something. |
| 347 | `vel_y = max(vel_y - GRAVITY * dt, -MAX_FALL);` | Applies gravity for this step, limited to the top falling speed. |
| 348 | `move_axis_x(wx * dt);` | Moves along x. |
| 349 | `move_axis_z(wz * dt);` | Moves along z. |
| 350 | `move_axis_y(vel_y * dt);` | Moves vertically last, so that `on_ground` reflects where the player ends up. |
| 351 | `}` | Ends `step_player` (line 336). |
| 353 | `fn look(dx, dy) {` | Turns the view from mouse movement. |
| 354 | `yaw = yaw + dx * MOUSE_SENSITIVITY;` | Moving the mouse right increases yaw, which turns right. |
| 355 | `pitch = min(MAX_PITCH, max(-MAX_PITCH, pitch - dy * MOUSE_SENSITIVITY));` | Moving the mouse down (positive dy) lowers pitch. The result is limited to the allowed range. |
| 356 | `}` | Ends `look` (line 353). |
| 358 | `fn sync_eye() {` | Puts the camera at the player's eyes. |
| 359 | `eye_x = px;` | Same x as the player. |
| 360 | `eye_y = py + EYE_HEIGHT;` | Eye height above the feet. |
| 361 | `eye_z = pz;` | Same z as the player. |
| 362 | `}` | Ends `sync_eye` (line 358). |

### Player: spawning

| Line | Code | What it does |
|---:|---|---|
| 364 | `// Starts on the open ground nearest the middle of the world, not on a treetop.` | What the function below looks for. |
| 365 | `fn spawn_player() {` | Chooses the player's starting position. |
| 366 | `let mid_x = SX / 2;` | World center, x. |
| 367 | `let mid_z = SZ / 2;` | World center, z. |
| 368 | `for (let r = 0; r < SX / 2; r = r + 1) {` | Searches outward in larger and larger squares. |
| 369 | `for (let dz = -r; dz <= r; dz = dz + 1) {` | Goes over z offsets in this square. |
| 370 | `for (let dx = -r; dx <= r; dx = dx + 1) {` | Goes over x offsets in this square. |
| 371 | `let h = terrain_height(mid_x + dx, mid_z + dz);` | Finds the terrain height there. |
| 372 | `if (get_block(mid_x + dx, h, mid_z + dz) == GRASS &&` | Looks for grass at the terrain surface, which rules out sand. |
| 373 | `get_block(mid_x + dx, h + 1, mid_z + dz) == AIR) {` | And air just above it, which rules out a tree trunk. |
| 374 | `px = mid_x + dx + 0.5;` | Centers the player in the column, x. |
| 375 | `py = h + 1 + COLLISION_GAP;` | Stands them on the surface, with the small gap. |
| 376 | `pz = mid_z + dz + 0.5;` | Centers the player in the column, z. |
| 377 | `ret;` | Done: leaves all three loops at once. |
| 378 | `}` | Ends the `if` block from line 373. |
| 379 | `}` | Ends the `for` loop from line 370. |
| 380 | `}` | Ends the `for` loop from line 369. |
| 381 | `}` | Ends the `for` loop from line 368. |
| 382 | `px = mid_x + 0.5;` | Fallback if no grass exists anywhere: center x. |
| 383 | `py = SY;` | Starts at the top of the world and lets them fall. |
| 384 | `pz = mid_z + 0.5;` | Center z. |
| 385 | `}` | Ends `spawn_player` (line 365). |

### Interaction: state

| Line | Code | What it does |
|---:|---|---|
| 387 | `// ---------- interaction ----------` | Section marker. |
| 389 | `let REACH = 6;` | How far away the player can reach, in blocks. |
| 390 | `let ACTION_REPEAT = 0.25;` | Seconds between repeated actions while a mouse button is held. |
| 391 | `let selected_block = GRASS;` | The block type that right click will place. |
| 392 | `let SLOT_KEYS = ["1", "2", "3", "4", "5", "6", "7", "8"];   // block type = slot + 1` | Keys that select a block. The key at array position 0 selects block type 1, and so on. |
| 394 | `// Results of the latest raycast. prev_* is the air cell in front of the hit block, and` | Introduces the raycast results below. `prev_*` is where a placed block would go. |
| 395 | `// hit_dir the direction of the face that was hit.` | And `hit_dir` is the direction of the face the ray entered. |
| 396 | `let ray_hit = false;` | Whether the ray found a block in reach. |
| 397 | `let hit_x = 0; let hit_y = 0; let hit_z = 0;` | Position of the block it hit. |
| 398 | `let prev_x = 0; let prev_y = 0; let prev_z = 0;` | The cell the ray was in just before that. |
| 399 | `let hit_dir = 0;` | Direction number of the face it hit. |

### Interaction: raycast

| Line | Code | What it does |
|---:|---|---|
| 401 | `fn axis_step(d) {` | Gives the direction to step along an axis, from the sign of `d`. |
| 402 | `if (d > 0) ret 1;` | Positive: step +1. |
| 403 | `if (d < 0) ret -1;` | Negative: step -1. |
| 404 | `ret 0;` | Zero: never step on this axis. |
| 405 | `}` | Ends `axis_step` (line 401). |
| 407 | `// Distance along the ray to the first grid plane on an axis.` | Explains the function below. |
| 408 | `fn first_crossing(origin, d) {` | How far along the ray it first crosses into the next cell on one axis. |
| 409 | `if (d > 0) ret (floor(origin) + 1 - origin) / d;` | Moving positive: distance to the next whole number, divided by speed along this axis. |
| 410 | `if (d < 0) ret (origin - floor(origin)) / -d;` | Moving negative: distance back to the previous whole number, divided by speed along this axis. |
| 411 | `ret 1000000;` | Not moving on this axis: returns a huge number so that this axis is never chosen. |
| 412 | `}` | Ends `first_crossing` (line 408). |
| 414 | `fn crossing_spacing(d) {` | How far along the ray it travels between crossings on one axis. |
| 415 | `if (d == 0) ret 1000000;` | Not moving on this axis: a huge number again, which avoids dividing by zero. |
| 416 | `ret 1 / abs(d);` | One cell divided by the speed along this axis. |
| 417 | `}` | Ends `crossing_spacing` (line 414). |
| 419 | `// Amanatides & Woo grid traversal along the view direction.` | Names the algorithm: it visits every cell the ray passes through, in order, and misses none. |
| 420 | `fn raycast() {` | Finds the block the crosshair is pointing at. The ray starts at the eye and travels along the forward vector. |
| 421 | `ray_hit = false;` | Assumes nothing is hit. |
| 422 | `let x = floor(eye_x); let y = floor(eye_y); let z = floor(eye_z);` | The cell containing the eye. |
| 423 | `let step_x = axis_step(fwd_x); let step_y = axis_step(fwd_y); let step_z = axis_step(fwd_z);` | Which way the ray steps on each axis. |
| 424 | `let tx = first_crossing(eye_x, fwd_x);` | Ray distance to the first x crossing. |
| 425 | `let ty = first_crossing(eye_y, fwd_y);` | Ray distance to the first y crossing. |
| 426 | `let tz = first_crossing(eye_z, fwd_z);` | Ray distance to the first z crossing. |
| 427 | `let dx = crossing_spacing(fwd_x);` | Ray distance between x crossings. |
| 428 | `let dy = crossing_spacing(fwd_y);` | Ray distance between y crossings. |
| 429 | `let dz = crossing_spacing(fwd_z);` | Ray distance between z crossings. |
| 430 | `let t = 0;` | Distance traveled so far. |
| 431 | `while (t <= REACH) {` | Continues until the ray goes out of reach. |
| 432 | `if (get_block(x, y, z) != AIR) {` | If the current cell is solid: |
| 433 | `ray_hit = true;` | Records a hit. |
| 434 | `hit_x = x; hit_y = y; hit_z = z;` | Records which block. If the eye itself starts inside a block, `prev_*` and `hit_dir` keep their values from an earlier ray. |
| 435 | `ret;` | Stops searching. |
| 436 | `}` | Ends the `if` block from line 432. |
| 437 | `prev_x = x; prev_y = y; prev_z = z;` | Remembers this empty cell, in case the next one is solid. |
| 438 | `if (tx <= ty && tx <= tz) {` | If the x crossing is nearest: |
| 439 | `x = x + step_x; t = tx; tx = tx + dx;` | Steps to the next cell along x, records the distance, and works out the following x crossing. |
| 440 | `if (step_x > 0) hit_dir = 1; else hit_dir = 0;` | Moving +x means it enters through the block's -X face (1); moving -x means the +X face (0). |
| 441 | `} else if (ty <= tz) {` | If the y crossing is nearest: |
| 442 | `y = y + step_y; t = ty; ty = ty + dy;` | The same step along y. |
| 443 | `if (step_y > 0) hit_dir = 3; else hit_dir = 2;` | Moving up enters through the bottom face (3); moving down, the top face (2). |
| 444 | `} else {` | Otherwise the z crossing is nearest: |
| 445 | `z = z + step_z; t = tz; tz = tz + dz;` | The same step along z. |
| 446 | `if (step_z > 0) hit_dir = 5; else hit_dir = 4;` | Moving +z enters through the -Z face (5); moving -z, the +Z face (4). |
| 447 | `}` | Ends the `else` branch from line 444. |
| 448 | `}` | Ends the `while` loop from line 431. |
| 449 | `}` | Ends `raycast` (line 420). |

### Interaction: editing blocks

| Line | Code | What it does |
|---:|---|---|
| 451 | `fn rebuild_chunk_at(x, z) {` | Rebuilds the chunk containing a position. |
| 452 | `if (x >= 0 && x < SX && z >= 0 && z < SZ) {` | Ignores positions outside the world. |
| 453 | `build_chunk(floor(x / CHUNK) + floor(z / CHUNK) * CHUNKS_X);` | Turns x and z into a chunk number and rebuilds that chunk's face list. |
| 454 | `}` | Ends the `if` block from line 452. |
| 455 | `}` | Ends `rebuild_chunk_at` (line 451). |
| 457 | `// A block on a chunk border also changes the neighboring chunk's faces.` | Why the function below may rebuild more than one chunk. |
| 458 | `fn block_changed(x, z) {` | Updates face lists after one block changes at (x, z). |
| 459 | `rebuild_chunk_at(x, z);` | Always rebuilds its own chunk. |
| 460 | `if ((x % CHUNK) == 0) rebuild_chunk_at(x - 1, z);` | On a chunk's low-x edge: rebuilds the neighbor at lower x too. |
| 461 | `if ((x % CHUNK) == CHUNK - 1) rebuild_chunk_at(x + 1, z);` | On its high-x edge: rebuilds the neighbor at higher x. |
| 462 | `if ((z % CHUNK) == 0) rebuild_chunk_at(x, z - 1);` | On its low-z edge: rebuilds the neighbor at lower z. |
| 463 | `if ((z % CHUNK) == CHUNK - 1) rebuild_chunk_at(x, z + 1);` | On its high-z edge: rebuilds the neighbor at higher z. |
| 464 | `}` | Ends `block_changed` (line 458). |
| 466 | `// The bottom layer stays, since the world has no undersides to show through a hole.` | Why the lowest layer can't be broken. |
| 467 | `fn break_block() {` | Removes the targeted block. Returns whether it did. |
| 468 | `if (!ray_hit \|\| hit_y == 0) ret false;` | Does nothing if no block is targeted, or if it is in the bottom layer. |
| 469 | `set_block(hit_x, hit_y, hit_z, AIR);` | Replaces the block with air. |
| 470 | `block_changed(hit_x, hit_z);` | Updates the visible faces. |
| 471 | `ret true;` | Reports success. |
| 472 | `}` | Ends `break_block` (line 467). |
| 474 | `fn box_overlaps_cell(x, y, z) {` | Checks whether the player's box overlaps block cell (x, y, z), so a block can't be placed on the player. |
| 475 | `let hw = PLAYER_HALF_WIDTH;` | Short name for the half width. |
| 476 | `ret px + hw > x && px - hw < x + 1 &&` | They overlap only if their ranges overlap on x... |
| 477 | `py + PLAYER_HEIGHT > y && py < y + 1 &&` | ...and on y... |
| 478 | `pz + hw > z && pz - hw < z + 1;` | ...and on z. |
| 479 | `}` | Ends `box_overlaps_cell` (line 474). |
| 481 | `fn place_block() {` | Places the selected block in the empty cell in front of the target. Returns whether it did. |
| 482 | `if (!ray_hit) ret false;` | Needs a targeted block. |
| 483 | `if (!in_world(prev_x, prev_y, prev_z)) ret false;` | That cell must be inside the world. |
| 484 | `if (get_block(prev_x, prev_y, prev_z) != AIR) ret false;` | And it must be empty. |
| 485 | `if (box_overlaps_cell(prev_x, prev_y, prev_z)) ret false;` | And it must not overlap the player. |
| 486 | `set_block(prev_x, prev_y, prev_z, selected_block);` | Stores the block. |
| 487 | `block_changed(prev_x, prev_z);` | Updates the visible faces. |
| 488 | `ret true;` | Reports success. |
| 489 | `}` | Ends `place_block` (line 481). |

### Rendering: buffers and tables

| Line | Code | What it does |
|---:|---|---|
| 491 | `// ---------- rendering ----------` | Section marker. |
| 493 | `let ATLAS_GRID = 4;` | The atlas image is a grid of 4 x 4 tiles. |
| 494 | `let TILE_SIZE = 1 / ATLAS_GRID;` | One tile's size in texture coordinates, where the whole image spans 0 to 1: 0.25. |
| 495 | `let UV_INSET = 0.002;        // keeps sampling from bleeding into the next tile` | A small margin inside each tile, so filtering or rounding can't pick up its neighbor's color. |
| 496 | `let TILE_DU = TILE_SIZE - 2 * UV_INSET;` | Width and height of the part of a tile that gets used. |
| 497 | `let TILE_U = [];` | Texture u of each tile's corner, filled at startup. |
| 498 | `let TILE_V = [];` | Texture v of each tile's corner, filled at startup. |
| 500 | `let VERT_STRIDE = 5;` | Numbers per vertex: x, y, u, v, color. This is the format `sdl_draw_tris` expects. |
| 501 | `let QUAD_STRIDE = 20;` | Numbers per quad: 4 vertices x 5. |
| 502 | `let verts = [];` | The vertex buffer, reused every frame. |
| 503 | `let indices = [];` | The index buffer, which says the order to draw in. |
| 504 | `let quad_keys = [];` | One depth key per quad, for sorting. |
| 505 | `let quad_count = 0;` | How many quads are in use this frame. |
| 506 | `let NUM_BUCKETS = 128;` | Number of distinct depth keys the sort handles. |
| 507 | `let bucket_counts = [];` | Working storage for the sort. |
| 509 | `// Packed vertex colors for each direction's shade, at each fog level.` | Introduces the color table below. |
| 510 | `let FOG_LEVELS = 16;` | Number of fog steps. |
| 511 | `let FOG_START = RENDER_RADIUS * 0.75;` | Fog starts at 75% of the render radius. |
| 512 | `let FOG_SCALE = FOG_LEVELS / (RENDER_RADIUS * RENDER_RADIUS - FOG_START * FOG_START);` | Converts squared distance into a fog step, so fog reaches its final step at the render radius. |
| 513 | `let DIR_SHADE = [0.72, 0.72, 1.0, 0.5, 0.85, 0.85];` | Brightness of each face direction: tops brightest, bottoms darkest, and the two side pairs different so that corners stand out. |
| 514 | `let colors = [];` | The color table. |
| 515 | `let COLOR_WHITE = 4294967295;` | Pure opaque white, all four color bytes at 255, packed into one number. It leaves the texture's colors unchanged. |
| 517 | `fn pack_rgba(r, g, b, a) { ret floor(r) * 16777216 + floor(g) * 65536 + floor(b) * 256 + floor(a); }` | Packs a color into one number the way `sdl_draw_tris` expects: red in the highest byte, alpha in the lowest. Uses arithmetic because the result can exceed the 32-bit signed range of bitwise operators. |
| 519 | `fn init_render_buffers() {` | Fills all the buffers once at startup, so nothing grows during play. |
| 520 | `for (let i = 0; i < ATLAS_GRID * ATLAS_GRID; i = i + 1) {` | For each of the 16 tiles: |
| 521 | `push(TILE_U, (i % ATLAS_GRID) * TILE_SIZE + UV_INSET);` | Its left edge: column times tile size, plus the margin. |
| 522 | `push(TILE_V, floor(i / ATLAS_GRID) * TILE_SIZE + UV_INSET);` | Its top edge: row times tile size, plus the margin. |
| 523 | `}` | Ends the `for` loop from line 520. |
| 524 | `for (let i = 0; i < MAX_QUADS * QUAD_STRIDE; i = i + 1) push(verts, 0);` | Sizes the vertex buffer. |
| 525 | `for (let i = 0; i < MAX_QUADS * 6; i = i + 1) push(indices, 0);` | Sizes the index buffer, 6 per quad because a quad is two triangles. |
| 526 | `for (let i = 0; i < MAX_QUADS; i = i + 1) push(quad_keys, 0);` | Sizes the key array. |
| 527 | `for (let i = 0; i < NUM_BUCKETS; i = i + 1) push(bucket_counts, 0);` | Sizes the sort's storage. |
| 528 | `for (let i = 0; i < 54; i = i + 1) push(view_defs, 0);` | Sizes the rotated face table. |
| 529 | `for (let i = 0; i < 6 * FOG_LEVELS; i = i + 1) push(colors, 0);` | Sizes the color table. |
| 530 | `}` | Ends `init_render_buffers` (line 519). |

### Rendering: colors

| Line | Code | What it does |
|---:|---|---|
| 532 | `fn update_colors(light) {` | Rebuilds the color table for a light level from 0 to 1. |
| 533 | `let k = 0;` | Write position in the table. |
| 534 | `for (let d = 0; d < 6; d = d + 1) {` | For each face direction: |
| 535 | `let c = 255 * DIR_SHADE[d] * light;` | Its gray level at this light. All three channels match, so light only darkens the texture. |
| 536 | `for (let f = 0; f < FOG_LEVELS; f = f + 1) {` | For each fog step: |
| 537 | `colors[k] = pack_rgba(c, c, c, 255 * (1 - f / (FOG_LEVELS - 1)));` | Stores the color, with alpha falling from 255 to 0 as fog increases. Far faces fade out and let whatever is behind show through. |
| 538 | `k = k + 1;` | Moves to the next slot. |
| 539 | `}` | Ends the `for` loop from line 536. |
| 540 | `}` | Ends the `for` loop from line 534. |
| 541 | `}` | Ends `update_colors` (line 532). |

### Rendering: near-plane clipping

| Line | Code | What it does |
|---:|---|---|
| 543 | `// Sutherland-Hodgman buffers: view x, y, z, u, v per polygon corner.` | Names the clipping method and says what the two buffers hold. |
| 544 | `let poly_in = [];` | Input polygon, the 4 corners of a quad. |
| 545 | `let poly_out = [];` | Output polygon after clipping. |
| 546 | `fn init_clip_buffers() {` | Sizes both buffers once at startup. |
| 547 | `for (let i = 0; i < 40; i = i + 1) { push(poly_in, 0); push(poly_out, 0); }` | Loops 40 times, adding one slot to each. A clipped polygon needs at most 25 (5 corners x 5 values), so this leaves room to spare. |
| 548 | `}` | Ends `init_clip_buffers` (line 546). |
| 550 | `fn write_vertex(k, vx, vy, vz, u, v, color) {` | Projects one camera-space point to the screen and writes the vertex at position `k`. |
| 551 | `let iz = FOCAL / vz;` | Perspective: things twice as far appear half as big. |
| 552 | `verts[k] = CX + vx * iz;` | Screen x. |
| 553 | `verts[k + 1] = CY - vy * iz;` | Screen y. It is subtracted because screen y grows downward while camera y grows upward. |
| 554 | `verts[k + 2] = u;` | Texture u. |
| 555 | `verts[k + 3] = v;` | Texture v. |
| 556 | `verts[k + 4] = color;` | Packed color. |
| 557 | `}` | Ends `write_vertex` (line 550). |
| 559 | `` // Writes polygon corner `c` as vertex slot `slot` of quad `q`. `` | Explains the function below. |
| 560 | `fn write_poly_vertex(q, slot, c, color) {` | Takes one corner of the clipped polygon and writes it into a quad. |
| 561 | `let p = c * VERT_STRIDE;` | Where this corner starts in `poly_out`. |
| 562 | `write_vertex(q * QUAD_STRIDE + slot * VERT_STRIDE,` | Works out the slot in the vertex buffer and starts the call to `write_vertex`. |
| 563 | `poly_out[p], poly_out[p + 1], poly_out[p + 2], poly_out[p + 3], poly_out[p + 4], color);` | Passes the corner's position, texture coordinates and color. |
| 564 | `}` | Ends `write_poly_vertex` (line 560). |
| 566 | `// Clips a quad of poly_in against the near plane, emitting 0, 1 or 2 quads. A` | Explains the function below: cutting a quad at the near plane leaves part of it in front of the camera. |
| 567 | `// clipped quad has 3 to 5 corners; a triangle repeats its last corner to stay a quad.` | That part has 3 to 5 corners. Everything is kept as quads so that the index pattern never changes, so a triangle repeats a corner. |
| 568 | `fn emit_clipped_quad(key, color) {` | Clips the quad in `poly_in` and adds the result to the frame. |
| 569 | `let n = 0;` | Values written to `poly_out` so far. |
| 570 | `for (let i = 0; i < 4; i = i + 1) {` | Goes around the quad's 4 edges. |
| 571 | `let a = i * VERT_STRIDE;` | Start of the edge, as a position in `poly_in`. |
| 572 | `let b = ((i + 1) % 4) * VERT_STRIDE;` | End of the edge, with `% 4` wrapping the last corner back to the first. |
| 573 | `let za = poly_in[a + 2];` | Depth of the edge's start. |
| 574 | `let zb = poly_in[b + 2];` | Depth of the edge's end. |
| 575 | `if (za >= NEAR) {` | If the start is in front of the near plane: |
| 576 | `for (let j = 0; j < VERT_STRIDE; j = j + 1) poly_out[n + j] = poly_in[a + j];` | Copies that corner to the output. |
| 577 | `n = n + VERT_STRIDE;` | Advances the output position. |
| 578 | `}` | Ends the `if` block from line 575. |
| 579 | `if ((za >= NEAR) != (zb >= NEAR)) {` | If the edge crosses the near plane, meaning exactly one end is in front: |
| 580 | `let t = (NEAR - za) / (zb - za);` | Finds how far along the edge the crossing is, from 0 to 1. |
| 581 | `for (let j = 0; j < VERT_STRIDE; j = j + 1) {` | For each of the 5 values: |
| 582 | `poly_out[n + j] = poly_in[a + j] + (poly_in[b + j] - poly_in[a + j]) * t;` | Blends between the ends, which adds a new corner exactly on the plane. |
| 583 | `}` | Ends the `for` loop from line 581. |
| 584 | `n = n + VERT_STRIDE;` | Advances the output position. |
| 585 | `}` | Ends the `if` block from line 579. |
| 586 | `}` | Ends the `for` loop from line 570. |
| 587 | `let corners = n / VERT_STRIDE;` | How many corners came out. |
| 588 | `if (corners < 3 \|\| quad_count + 2 > MAX_QUADS) ret;` | Stops if the quad was entirely behind the plane or the buffer is nearly full. |
| 590 | `let q = quad_count;` | The next free quad. |
| 591 | `write_poly_vertex(q, 0, 0, color);` | Writes corner 0. |
| 592 | `write_poly_vertex(q, 1, 1, color);` | Writes corner 1. |
| 593 | `write_poly_vertex(q, 2, 2, color);` | Writes corner 2. |
| 594 | `if (corners >= 4) write_poly_vertex(q, 3, 3, color); else write_poly_vertex(q, 3, 2, color);` | Writes corner 3, or repeats corner 2 if there are only 3 corners. That collapses the quad into a triangle. |
| 595 | `quad_keys[q] = key;` | Stores the quad's sort key. |
| 596 | `q = q + 1;` | Moves on to the next quad. |
| 597 | `if (corners == 5) {` | With 5 corners, a second quad covers the rest: |
| 598 | `write_poly_vertex(q, 0, 0, color);` | It shares corner 0. |
| 599 | `write_poly_vertex(q, 1, 3, color);` | Then corner 3. |
| 600 | `write_poly_vertex(q, 2, 4, color);` | Then corner 4. |
| 601 | `write_poly_vertex(q, 3, 4, color);` | Corner 4 again, making this quad a triangle. |
| 602 | `quad_keys[q] = key;` | Stores its sort key. |
| 603 | `q = q + 1;` | Counts it. |
| 604 | `}` | Ends the `if` block from line 597. |
| 605 | `quad_count = q;` | Saves the new quad count. |
| 606 | `}` | Ends `emit_clipped_quad` (line 568). |

### Rendering: near faces

| Line | Code | What it does |
|---:|---|---|
| 608 | `fn set_poly_corner(c, vx, vy, vz, u, v) {` | Sets one corner of the input polygon. |
| 609 | `let p = c * VERT_STRIDE;` | Where this corner starts. |
| 610 | `poly_in[p] = vx;` | Camera-space x. |
| 611 | `poly_in[p + 1] = vy;` | Camera-space y. |
| 612 | `poly_in[p + 2] = vz;` | Depth. |
| 613 | `poly_in[p + 3] = u;` | Texture u. |
| 614 | `poly_in[p + 4] = v;` | Texture v. |
| 615 | `}` | Ends `set_poly_corner` (line 608). |
| 617 | `// Draws one face in N x N pieces, each clipped on its own. Used for faces near the` | Explains the function below, and why near faces get split. |
| 618 | `// camera, where a single quad would visibly warp or cross the near plane.` | Small pieces warp much less, and each is clipped separately. |
| 619 | `fn emit_near_face(vx, vy, vz, d, u0, v0, key, color, n) {` | Draws a face in n x n pieces. (vx, vy, vz) is its first corner in camera space. |
| 620 | `let o = d * 9;` | Where this direction starts in `view_defs`. |
| 621 | `let step = 1 / n;` | Size of one piece, as a fraction of the face. |
| 622 | `let ax = view_defs[o + 3] * step;` | One piece's u edge, x part. |
| 623 | `let ay = view_defs[o + 4] * step;` | One piece's u edge, y part. |
| 624 | `let az = view_defs[o + 5] * step;` | One piece's u edge, z part. |
| 625 | `let bx = view_defs[o + 6] * step;` | One piece's v edge, x part. |
| 626 | `let by = view_defs[o + 7] * step;` | One piece's v edge, y part. |
| 627 | `let bz = view_defs[o + 8] * step;` | One piece's v edge, z part. |
| 628 | `let du = TILE_DU * step;` | Texture size of one piece. |
| 629 | `for (let j = 0; j < n; j = j + 1) {` | For each row of pieces: |
| 630 | `for (let i = 0; i < n; i = i + 1) {` | For each piece in the row: |
| 631 | `let x = vx + ax * i + bx * j;` | Its first corner, x. |
| 632 | `let y = vy + ay * i + by * j;` | Its first corner, y. |
| 633 | `let z = vz + az * i + bz * j;` | Its first corner, z. |
| 634 | `let u = u0 + du * i;` | Its texture u. |
| 635 | `let v = v0 + du * j;` | Its texture v. |
| 636 | `set_poly_corner(0, x, y, z, u, v);` | Sets corner 0. |
| 637 | `set_poly_corner(1, x + ax, y + ay, z + az, u + du, v);` | Sets corner 1, one u edge along. |
| 638 | `set_poly_corner(2, x + ax + bx, y + ay + by, z + az + bz, u + du, v + du);` | Sets corner 2, one u edge and one v edge along. |
| 639 | `set_poly_corner(3, x + bx, y + by, z + bz, u, v + du);` | Sets corner 3, one v edge along. |
| 640 | `emit_clipped_quad(key, color);` | Clips and emits this piece. |
| 641 | `}` | Ends the `for` loop from line 630. |
| 642 | `}` | Ends the `for` loop from line 629. |
| 643 | `}` | Ends `emit_near_face` (line 619). |

### Rendering: culling

| Line | Code | What it does |
|---:|---|---|
| 645 | `// Rough test of a sphere of radius r around a view-space point against the frustum.` | Explains the function below. |
| 646 | `fn sphere_visible(vx, vy, vz, r) {` | Tests whether a sphere could be on screen. Used to skip whole chunks. |
| 647 | `if (vz < -r) ret false;` | Entirely behind the camera: not visible. |
| 648 | `let lim = (vz + r) * CULL_SLOPE + r;` | The horizontal limit at this depth, grown by the sphere's size. |
| 649 | `if (vx > lim \|\| vx < -lim) ret false;` | Too far left or right: not visible. |
| 650 | `lim = (vz + r) * CULL_SLOPE_Y + r;` | The vertical limit at this depth. |
| 651 | `ret vy <= lim && vy >= -lim;` | Visible if it is within the vertical limit too. |
| 652 | `}` | Ends `sphere_visible` (line 646). |

### Rendering: projecting a chunk

| Line | Code | What it does |
|---:|---|---|
| 654 | `// Projects the front-facing, in-range faces of one chunk into the vertex buffer.` | Explains the function below. This is the game's hottest code, which is why it avoids function calls where it can. |
| 655 | `fn project_chunk(faces) {` | Turns one chunk's faces into screen-space quads. |
| 656 | `let ex = eye_x; let ey = eye_y; let ez = eye_z;` | Camera position, copied to locals because locals are the fastest variables in Lunar. The lines through 683 do the same for everything used in the loop. |
| 657 | `let rx = rgt_x; let ry = rgt_y; let rz = rgt_z;` | Right vector. |
| 658 | `let ux = up_x;  let uy = up_y;  let uz = up_z;` | Up vector. |
| 659 | `let fx = fwd_x; let fy = fwd_y; let fz = fwd_z;` | Forward vector. |
| 660 | `let cell_x = floor(ex); let cell_y = floor(ey); let cell_z = floor(ez);` | The camera's cell, used for the sort key. |
| 661 | `let radius2 = RENDER_RADIUS * RENDER_RADIUS;` | Squared render radius. Comparing squares avoids a square root per face. |
| 662 | `let fog_start2 = FOG_START * FOG_START;` | Squared fog start. |
| 663 | `let fog_scale = FOG_SCALE;` | Local copy of the fog scale. |
| 664 | `let fog_levels = FOG_LEVELS;` | Local copy of the fog step count. |
| 665 | `let slope_x = CULL_SLOPE;` | Local copy of the horizontal culling slope. |
| 666 | `let slope_y = CULL_SLOPE_Y;` | Local copy of the vertical culling slope. |
| 667 | `let focal = FOCAL;` | Local copy of the focal length. |
| 668 | `let cx = CX; let cy = CY;` | Local copy of the screen center. |
| 669 | `let near_dist = SUBDIVIDE_DIST;` | Local copy of the subdivision distance. |
| 670 | `let near = NEAR;` | Local copy of the near plane. Not used below this point. |
| 671 | `let max_quads = MAX_QUADS;` | Local copy of the quad limit. |
| 672 | `let last_bucket = NUM_BUCKETS - 1;` | Highest allowed sort key. |
| 673 | `let du = TILE_DU;` | Local copy of the tile size. |
| 674 | `let vd = view_defs;` | Local reference to the rotated face table. Arrays are shared, not copied. |
| 675 | `let planes = CAM_PLANE;` | Local reference to the camera planes. |
| 676 | `let out = verts;` | Local reference to the vertex buffer. |
| 677 | `let keys = quad_keys;` | Local reference to the key array. |
| 678 | `let tile_u = TILE_U;` | Local reference to the tile u table. |
| 679 | `let tile_v = TILE_V;` | Local reference to the tile v table. |
| 680 | `let palette = colors;` | Local reference to the color table. |
| 681 | `let stride = FACE_STRIDE;` | Local copy of the record size. |
| 682 | `let n = len(faces);` | Numbers in this chunk's face list. |
| 683 | `let q = quad_count;` | Local copy of the quad count, saved back at the end. |
| 685 | `for (let i = 0; i < n; i = i + stride) {` | Goes through the records. |
| 686 | `let d = faces[i + 3];` | This face's direction. |
| 687 | `if (planes[d] > faces[i + 5]) {` | Back-face test: continues only if the camera is on the face's outer side. Most hidden faces stop here, cheaply. |
| 688 | `let x = faces[i];` | Block x. |
| 689 | `let y = faces[i + 1];` | Block y. |
| 690 | `let z = faces[i + 2];` | Block z. |
| 691 | `let dx = x - ex;` | Offset from the camera, x. |
| 692 | `let dy = y - ey;` | Offset from the camera, y. |
| 693 | `let dz = z - ez;` | Offset from the camera, z. |
| 694 | `let dist2 = dx * dx + dz * dz;` | Squared horizontal distance. |
| 695 | `if (dist2 < radius2) {` | Continues only if within the render radius. |
| 696 | `let vz = dx * fx + dy * fy + dz * fz;` | Block corner's depth in camera space. |
| 697 | `let vx = dx * rx + dz * rz;` | Its camera-space x. The right vector has no y part, so that term is left out. |
| 698 | `let vy = dx * ux + dy * uy + dz * uz;` | Its camera-space y. |
| 699 | `let lim_x = (vz + 2) * slope_x + 2;` | Horizontal limit at this depth, with a margin of 2 to cover the size of a block. |
| 700 | `let lim_y = (vz + 2) * slope_y + 2;` | Vertical limit at this depth. |
| 701 | `if (vz > -2 && vx < lim_x && vx > -lim_x && vy < lim_y && vy > -lim_y) {` | Continues only if the block isn't well behind the camera or outside the view. |
| 702 | `// Any ray from the eye enters grid cells in strictly increasing` | Introduces the sort key. Walking along a ray, each new cell is farther from the camera's cell in steps along the grid. |
| 703 | `// Manhattan offset from the eye's cell, so this key alone gives an` | So anything that blocks a cell always has a smaller key than that cell. |
| 704 | `// exact back-to-front order.` | Sorting by this key is therefore enough to draw back to front correctly. |
| 705 | `let kx = x - cell_x; if (kx < 0) kx = -kx;` | Grid distance along x, made positive without a function call. |
| 706 | `let ky = y - cell_y; if (ky < 0) ky = -ky;` | Grid distance along y. |
| 707 | `let kz = z - cell_z; if (kz < 0) kz = -kz;` | Grid distance along z. |
| 708 | `let key = kx + ky + kz;` | Adds them to get the key. |
| 709 | `if (key > last_bucket) key = last_bucket;` | Limits it to the available buckets. |
| 711 | `let fog = 0;` | No fog by default. |
| 712 | `if (dist2 > fog_start2) {` | If beyond the fog start: |
| 713 | `fog = floor((dist2 - fog_start2) * fog_scale);` | Converts distance to a fog step. |
| 714 | `if (fog >= fog_levels) fog = fog_levels - 1;` | Limits it to the last step. |
| 715 | `}` | Ends the `if` block from line 712. |
| 716 | `let color = palette[d * fog_levels + fog];` | Gets the color for this direction and fog step. |
| 718 | `let o = d * 9;` | Where this direction starts in the rotated table. |
| 719 | `let x0 = vx + vd[o];` | Face's first corner in camera space, x. |
| 720 | `let y0 = vy + vd[o + 1];` | y. |
| 721 | `let z0 = vz + vd[o + 2];` | z (depth). |
| 722 | `let ax = vd[o + 3]; let ay = vd[o + 4]; let az = vd[o + 5];` | The u edge in camera space. |
| 723 | `let bx = vd[o + 6]; let by = vd[o + 7]; let bz = vd[o + 8];` | The v edge in camera space. |
| 724 | `let z1 = z0 + az;` | Depth of corner 1. |
| 725 | `let z2 = z1 + bz;` | Depth of corner 2. |
| 726 | `let z3 = z0 + bz;` | Depth of corner 3. |
| 727 | `let t = faces[i + 4];` | This face's atlas tile. |
| 728 | `let u0 = tile_u[t];` | Tile's u. |
| 729 | `let v0 = tile_v[t];` | Tile's v. |
| 731 | `if (z0 < near_dist \|\| z1 < near_dist \|\| z2 < near_dist \|\| z3 < near_dist) {` | If any corner is close, this face takes the slower, careful path. Clipping happens there too, so the near plane is always handled. |
| 732 | `quad_count = q;` | Saves the count, because the function below changes it. |
| 733 | `emit_near_face(x0, y0, z0, d, u0, v0, key, color, SUBDIVISIONS);` | Draws the face in small clipped pieces. |
| 734 | `q = quad_count;` | Picks up the new count. |
| 735 | `} else if (q < max_quads) {` | Otherwise the fast path, if there's room: |
| 736 | `let k = q * 20;` | Where this quad starts in the vertex buffer. |
| 737 | `let iz = focal / z0;` | Perspective factor for corner 0. |
| 738 | `out[k] = cx + x0 * iz;` | Corner 0, screen x. |
| 739 | `out[k + 1] = cy - y0 * iz;` | Corner 0, screen y. |
| 740 | `out[k + 2] = u0;` | Corner 0, texture u. |
| 741 | `out[k + 3] = v0;` | Corner 0, texture v. |
| 742 | `out[k + 4] = color;` | Corner 0, color. |
| 743 | `let x1 = x0 + ax; let y1 = y0 + ay;` | Corner 1 in camera space: corner 0 plus the u edge. |
| 744 | `iz = focal / z1;` | Perspective factor for corner 1. |
| 745 | `out[k + 5] = cx + x1 * iz;` | Corner 1, screen x. |
| 746 | `out[k + 6] = cy - y1 * iz;` | Corner 1, screen y. |
| 747 | `out[k + 7] = u0 + du;` | Corner 1, texture u, one tile across. |
| 748 | `out[k + 8] = v0;` | Corner 1, texture v. |
| 749 | `out[k + 9] = color;` | Corner 1, color. |
| 750 | `let x2 = x1 + bx; let y2 = y1 + by;` | Corner 2 in camera space: corner 1 plus the v edge. |
| 751 | `iz = focal / z2;` | Perspective factor for corner 2. |
| 752 | `out[k + 10] = cx + x2 * iz;` | Corner 2, screen x. |
| 753 | `out[k + 11] = cy - y2 * iz;` | Corner 2, screen y. |
| 754 | `out[k + 12] = u0 + du;` | Corner 2, texture u. |
| 755 | `out[k + 13] = v0 + du;` | Corner 2, texture v, one tile down. |
| 756 | `out[k + 14] = color;` | Corner 2, color. |
| 757 | `iz = focal / z3;` | Perspective factor for corner 3. |
| 758 | `out[k + 15] = cx + (x0 + bx) * iz;` | Corner 3, screen x: corner 0 plus the v edge. |
| 759 | `out[k + 16] = cy - (y0 + by) * iz;` | Corner 3, screen y. |
| 760 | `out[k + 17] = u0;` | Corner 3, texture u. |
| 761 | `out[k + 18] = v0 + du;` | Corner 3, texture v. |
| 762 | `out[k + 19] = color;` | Corner 3, color. |
| 763 | `keys[q] = key;` | Stores the sort key. |
| 764 | `q = q + 1;` | Counts the quad. |
| 765 | `}` | Ends the `else` branch from line 735. |
| 766 | `}` | Ends the `if` block from line 701. |
| 767 | `}` | Ends the `if` block from line 695. |
| 768 | `}` | Ends the `if` block from line 687. |
| 769 | `}` | Ends the `for` loop from line 685. |
| 770 | `quad_count = q;` | Saves the count back to the shared variable. |
| 771 | `}` | Ends `project_chunk` (line 655). |

### Rendering: projecting the world

| Line | Code | What it does |
|---:|---|---|
| 773 | `fn project_world() {` | Projects every visible chunk for this frame. |
| 774 | `quad_count = 0;` | Starts with no quads. |
| 775 | `let half = CHUNK / 2;` | Half a chunk's width. |
| 776 | `let chunk_radius = 12;  // half-diagonal of an 8 x 16 x 8 chunk` | A sphere radius certain to contain a chunk. The exact half-diagonal is about 9.8, so 12 is on the safe side. |
| 777 | `let reach = RENDER_RADIUS + chunk_radius;` | Farthest a chunk's center can be and still have a block in range. |
| 778 | `for (let i = 0; i < CHUNKS_X * CHUNKS_Z; i = i + 1) {` | For each chunk: |
| 779 | `let dx = (i % CHUNKS_X) * CHUNK + half - eye_x;` | Offset to its center, x. |
| 780 | `let dy = SY / 2 - eye_y;` | Offset to its center, y. |
| 781 | `let dz = floor(i / CHUNKS_X) * CHUNK + half - eye_z;` | Offset to its center, z. |
| 782 | `if (dx * dx + dz * dz < reach * reach) {` | If it is within range: |
| 783 | `let vx = dx * rgt_x + dy * rgt_y + dz * rgt_z;` | Center in camera space, x. |
| 784 | `let vy = dx * up_x + dy * up_y + dz * up_z;` | Center in camera space, y. |
| 785 | `let vz = dx * fwd_x + dy * fwd_y + dz * fwd_z;` | Center in camera space, z. |
| 786 | `if (sphere_visible(vx, vy, vz, chunk_radius)) project_chunk(chunk_faces[i]);` | Projects the chunk if its sphere could be on screen. |
| 787 | `}` | Ends the `if` block from line 782. |
| 788 | `}` | Ends the `for` loop from line 778. |
| 789 | `}` | Ends `project_world` (line 773). |

### Rendering: depth sort

| Line | Code | What it does |
|---:|---|---|
| 791 | `// Counting sort of the quads, farthest bucket first, written out as indices.` | Explains the function below. Counting sort is linear time because keys are small whole numbers. |
| 792 | `fn sort_quads() {` | Writes the draw order into the index buffer. |
| 793 | `let n = quad_count;` | Number of quads. |
| 794 | `let keys = quad_keys;` | Local reference to the keys. |
| 795 | `let counts = bucket_counts;` | Local reference to the counters. |
| 796 | `let out = indices;` | Local reference to the index buffer. |
| 797 | `let buckets = NUM_BUCKETS;` | Local copy of the bucket count. |
| 798 | `for (let b = 0; b < buckets; b = b + 1) counts[b] = 0;` | Resets the counters. |
| 799 | `for (let q = 0; q < n; q = q + 1) {` | For each quad: |
| 800 | `let k = keys[q];` | Its key. |
| 801 | `counts[k] = counts[k] + 1;` | Counts it in that bucket. |
| 802 | `}` | Ends the `for` loop from line 799. |
| 803 | `let start = 0;` | Index-buffer position of the next bucket. |
| 804 | `for (let b = buckets - 1; b >= 0; b = b - 1) {` | Goes from the farthest bucket to the nearest, so far quads get drawn first. |
| 805 | `let c = counts[b];` | How many quads are in this bucket. |
| 806 | `counts[b] = start;` | Turns the counter into the bucket's start position. |
| 807 | `start = start + c * 6;` | Leaves room for its quads, at 6 indices each. |
| 808 | `}` | Ends the `for` loop from line 804. |
| 809 | `for (let q = 0; q < n; q = q + 1) {` | For each quad again: |
| 810 | `let k = keys[q];` | Its key. |
| 811 | `let at = counts[k];` | Where it goes. |
| 812 | `counts[k] = at + 6;` | Moves that bucket's position along for the next quad. |
| 813 | `let v = q * 4;` | Its first vertex number. Each quad has 4 vertices. |
| 814 | `out[at] = v;` | First triangle: corner 0. |
| 815 | `out[at + 1] = v + 1;` | Corner 1. |
| 816 | `out[at + 2] = v + 2;` | Corner 2. |
| 817 | `out[at + 3] = v;` | Second triangle: corner 0. |
| 818 | `out[at + 4] = v + 2;` | Corner 2. |
| 819 | `out[at + 5] = v + 3;` | Corner 3. |
| 820 | `}` | Ends the `for` loop from line 809. |
| 821 | `}` | Ends `sort_quads` (line 792). |

### Rendering: block highlight

| Line | Code | What it does |
|---:|---|---|
| 823 | `// Outlines the targeted face, after everything else so it draws on top. It needs no` | Explains why the highlight goes last. |
| 824 | `// depth test: the ray reached this face, so little can be in front of it.` | A separate depth check isn't needed, because the ray reached this face without being blocked. |
| 825 | `fn emit_highlight() {` | Adds the outline for the targeted face. |
| 826 | `if (!ray_hit) ret;` | Nothing is targeted: nothing to do. |
| 827 | `let first = quad_count;` | Remembers where the new quads will start. |
| 828 | `let dx = hit_x - eye_x;` | Offset from the camera to the block, x. |
| 829 | `let dy = hit_y - eye_y;` | y. |
| 830 | `let dz = hit_z - eye_z;` | z. |
| 831 | `let o = hit_dir * 9;` | Where the hit direction starts in the rotated table. |
| 832 | `emit_near_face(dx * rgt_x + dz * rgt_z + view_defs[o],` | Reuses `emit_near_face`. First argument: the face corner's camera-space x. |
| 833 | `dx * up_x + dy * up_y + dz * up_z + view_defs[o + 1],` | Its camera-space y. |
| 834 | `dx * fwd_x + dy * fwd_y + dz * fwd_z + view_defs[o + 2],` | Its depth. |
| 835 | `hit_dir, TILE_U[TILE_HIGHLIGHT], TILE_V[TILE_HIGHLIGHT], 0, COLOR_WHITE, 1);` | Then the direction, the outline tile, key 0, white, and 1 piece so that the outline tile isn't repeated. |
| 836 | `for (let q = first; q < quad_count; q = q + 1) {` | For each new quad: |
| 837 | `let at = q * 6;` | Where its indices go. They come after the sorted quads, so it draws on top. |
| 838 | `let v = q * 4;` | Its first vertex number. |
| 839 | `indices[at] = v;` | First triangle: corner 0. |
| 840 | `indices[at + 1] = v + 1;` | Corner 1. |
| 841 | `indices[at + 2] = v + 2;` | Corner 2. |
| 842 | `indices[at + 3] = v;` | Second triangle: corner 0. |
| 843 | `indices[at + 4] = v + 2;` | Corner 2. |
| 844 | `indices[at + 5] = v + 3;` | Corner 3. |
| 845 | `}` | Ends the `for` loop from line 836. |
| 846 | `}` | Ends `emit_highlight` (line 825). |

### HUD: data

| Line | Code | What it does |
|---:|---|---|
| 848 | `// ---------- HUD ----------` | Section marker. |
| 850 | `let atlas = -1;` | Handle of the atlas texture. -1 means not loaded yet. |
| 852 | `// 3x5 pixel glyphs, one bit per pixel, top row in the high bits. Digits, then F, P, S.` | Explains the font: each glyph is a 15-bit number, one bit per pixel of a 3 x 5 grid. |
| 853 | `let GLYPHS = [31599, 11415, 29671, 29647, 23497, 31183, 31215, 29257, 31727, 31695, 31204, 31716, 31183];` | The font. Position in the array is the digit, then F, P and S. S looks the same as 5. |
| 854 | `let GLYPH_F = 10;` | Position of F in the font. |
| 855 | `let GLYPH_P = 11;` | Position of P. |
| 856 | `let GLYPH_S = 12;` | Position of S. |
| 857 | `let SLOT_SIZE = 44;` | Size of one hotbar slot, in pixels. |
| 858 | `let SLOT_COUNT = len(SLOT_KEYS);` | Number of slots, 8. |
| 859 | `let hotbar_x = (WIN_W - SLOT_COUNT * SLOT_SIZE) / 2;` | Left edge that centers the hotbar. |
| 860 | `let hotbar_y = WIN_H - SLOT_SIZE - 10;` | Top edge, 10 pixels above the window bottom. |
| 861 | `let icon_verts = [];` | Vertices of the hotbar icons. |
| 862 | `let icon_indices = [];` | Indices of the hotbar icons. |

### HUD: setup

| Line | Code | What it does |
|---:|---|---|
| 864 | `fn add_icon_vertex(x, y, u, v) {` | Adds one vertex to the icon list. |
| 865 | `push(icon_verts, x); push(icon_verts, y); push(icon_verts, u); push(icon_verts, v);` | Its position and texture coordinates. |
| 866 | `push(icon_verts, COLOR_WHITE);` | Its color, white, so the texture shows unchanged. |
| 867 | `}` | Ends `add_icon_vertex` (line 864). |
| 869 | `fn init_hud() {` | Builds the hotbar icons once. They never move, so they aren't rebuilt. |
| 870 | `let pad = 6;` | Space around an icon inside its slot. |
| 871 | `let size = SLOT_SIZE - 2 * pad;` | Icon size. |
| 872 | `for (let i = 0; i < SLOT_COUNT; i = i + 1) {` | For each slot: |
| 873 | `let t = TILE_SIDE[i + 1];` | Slot 0 holds block 1, so this picks the tile of block `i + 1`. |
| 874 | `let u = TILE_U[t];` | Tile's u. |
| 875 | `let v = TILE_V[t];` | Tile's v. |
| 876 | `let x = hotbar_x + i * SLOT_SIZE + pad;` | Icon's left edge. |
| 877 | `let y = hotbar_y + pad;` | Icon's top edge. |
| 878 | `add_icon_vertex(x, y, u, v);` | Top-left corner. |
| 879 | `add_icon_vertex(x + size, y, u + TILE_DU, v);` | Top-right corner. |
| 880 | `add_icon_vertex(x + size, y + size, u + TILE_DU, v + TILE_DU);` | Bottom-right corner. |
| 881 | `add_icon_vertex(x, y + size, u, v + TILE_DU);` | Bottom-left corner. |
| 882 | `let b = i * 4;` | This icon's first vertex number. |
| 883 | `push(icon_indices, b); push(icon_indices, b + 1); push(icon_indices, b + 2);` | First triangle. |
| 884 | `push(icon_indices, b); push(icon_indices, b + 2); push(icon_indices, b + 3);` | Second triangle. |
| 885 | `}` | Ends the `for` loop from line 872. |
| 886 | `}` | Ends `init_hud` (line 869). |

### HUD: text

| Line | Code | What it does |
|---:|---|---|
| 888 | `fn draw_glyph(g, x, y, s) {` | Draws one glyph at (x, y), with pixels `s` wide. |
| 889 | `let bits = GLYPHS[g];` | Gets its bits. |
| 890 | `for (let row = 0; row < 5; row = row + 1) {` | For each of the 5 rows: |
| 891 | `for (let col = 0; col < 3; col = col + 1) {` | For each of the 3 columns: |
| 892 | `if (((bits >> (14 - row * 3 - col)) & 1) == 1) {` | Picks this pixel's bit: the top-left pixel is bit 14, and it counts down from there. If it is set: |
| 893 | `sdl_fill_rect(x + col * s, y + row * s, s, s, 255, 255, 255, 255);` | Draws a square. |
| 894 | `}` | Ends the `if` block from line 892. |
| 895 | `}` | Ends the `for` loop from line 891. |
| 896 | `}` | Ends the `for` loop from line 890. |
| 897 | `}` | Ends `draw_glyph` (line 888). |
| 899 | `// Draws a non-negative whole number with its last digit ending at x.` | Explains the function below. |
| 900 | `fn draw_number(n, x, y, s) {` | Draws a number from right to left. |
| 901 | `let v = n;` | Working copy of the number. |
| 902 | `while (v >= 10) {` | While more than one digit remains: |
| 903 | `draw_glyph(v - floor(v / 10) * 10, x, y, s);` | Draws the last digit. Lunar's `%` would work here too, but this version is plain arithmetic. |
| 904 | `v = floor(v / 10);` | Removes that digit. |
| 905 | `x = x - 4 * s;` | Moves left by one character: 3 pixels plus a 1-pixel gap, all scaled by `s`. |
| 906 | `}` | Ends the `while` loop from line 902. |
| 907 | `draw_glyph(v, x, y, s);` | Draws the final, leftmost digit. |
| 908 | `}` | Ends `draw_number` (line 900). |

### HUD: drawing

| Line | Code | What it does |
|---:|---|---|
| 910 | `fn draw_hud(fps) {` | Draws everything on top of the world. `fps` is the number to display. |
| 911 | `// crosshair` | Label for the next two lines. |
| 912 | `sdl_fill_rect(CX - 8, CY - 1, 16, 2, 255, 255, 255, 220);` | Horizontal bar of the crosshair. |
| 913 | `sdl_fill_rect(CX - 1, CY - 8, 2, 16, 255, 255, 255, 220);` | Vertical bar of the crosshair. |
| 915 | `// hotbar` | Label for the next lines. |
| 916 | `sdl_fill_rect(hotbar_x, hotbar_y, SLOT_COUNT * SLOT_SIZE, SLOT_SIZE, 0, 0, 0, 110);` | Translucent dark background for the hotbar. |
| 917 | `sdl_draw_tris(atlas, icon_verts, SLOT_COUNT * 4, icon_indices, SLOT_COUNT * 6);` | Draws all the block icons in one call. |
| 918 | `let sel = hotbar_x + (selected_block - 1) * SLOT_SIZE;` | Left edge of the selected slot. |
| 919 | `sdl_fill_rect(sel, hotbar_y, SLOT_SIZE, 3, 255, 255, 255, 255);` | Its top border. |
| 920 | `sdl_fill_rect(sel, hotbar_y + SLOT_SIZE - 3, SLOT_SIZE, 3, 255, 255, 255, 255);` | Its bottom border. |
| 921 | `sdl_fill_rect(sel, hotbar_y, 3, SLOT_SIZE, 255, 255, 255, 255);` | Its left border. |
| 922 | `sdl_fill_rect(sel + SLOT_SIZE - 3, hotbar_y, 3, SLOT_SIZE, 255, 255, 255, 255);` | Its right border. |
| 924 | `// FPS` | Label for the next lines. |
| 925 | `sdl_fill_rect(6, 6, 104, 24, 0, 0, 0, 110);` | Translucent dark box behind the text. |
| 926 | `draw_glyph(GLYPH_F, 12, 10, 3);` | The letter F. |
| 927 | `draw_glyph(GLYPH_P, 24, 10, 3);` | The letter P. |
| 928 | `draw_glyph(GLYPH_S, 36, 10, 3);` | The letter S. |
| 929 | `draw_number(floor(fps), 96, 10, 3);` | The FPS value, rounded down, with its last digit ending at x = 96. |
| 930 | `}` | Ends `draw_hud` (line 910). |

### Day and night

| Line | Code | What it does |
|---:|---|---|
| 932 | `// ---------- day and night ----------` | Section marker. |
| 934 | `let DAY_LENGTH = 240;     // seconds` | Length of a full day and night, in seconds. |
| 935 | `let MIN_LIGHT = 0.28;` | Brightness at midnight, so the world never goes fully black. |
| 936 | `let sky_r = 135;` | Sky red. Starting value; set from the first frame on. |
| 937 | `let sky_g = 190;` | Sky green. |
| 938 | `let sky_b = 235;` | Sky blue. |
| 940 | `// Starts at noon. Far faces fade toward whatever is behind them, so the fog follows` | Says the cycle starts at noon. Far faces fade through alpha, not toward a fixed color... |
| 941 | `// the sky color by itself.` | ...so fog always matches the sky, even at night, with no extra work. |
| 942 | `fn update_daylight(seconds) {` | Sets the light and sky color for a point in time. |
| 943 | `let sun = (cos(seconds / DAY_LENGTH * 6.2831853) + 1) / 2;` | Sun strength: 1 at noon, 0 at midnight, following a cosine wave. |
| 944 | `let light = MIN_LIGHT + (1 - MIN_LIGHT) * sun;` | Maps that to the range from `MIN_LIGHT` to 1. |
| 945 | `update_colors(light);` | Rebuilds the color table. |
| 946 | `sky_r = 12 + 123 * sun;` | Sky red: 12 at night to 135 by day. |
| 947 | `sky_g = 16 + 174 * sun;` | Sky green: 16 to 190. |
| 948 | `sky_b = 40 + 195 * sun;` | Sky blue: 40 to 235. |
| 949 | `}` | Ends `update_daylight` (line 942). |

### Main: asset loading and timings

| Line | Code | What it does |
|---:|---|---|
| 951 | `// ---------- main ----------` | Section marker. |
| 953 | `fn load_atlas() {` | Loads the block textures. Returns whether it worked. |
| 954 | `atlas = sdl_load_texture("examples/minecraft/atlas.png");` | Tries the path used when running from the repo root. |
| 955 | `if (atlas < 0) atlas = sdl_load_texture("atlas.png");` | If that failed, tries the path used when running from this folder. |
| 956 | `ret atlas >= 0;` | Reports success. A failed first attempt also prints an error message, which is harmless if the second one works. |
| 957 | `}` | Ends `load_atlas` (line 953). |
| 959 | `let PHASE_NAMES = ["input+physics", "project", "sort", "draw", "present"];` | Names of the timed phases of a frame, printed in the terminal. |
| 960 | `let phase_times = [0, 0, 0, 0, 0];` | Seconds spent in each phase since the last printout. |
| 962 | `fn print_timings(frames) {` | Prints average phase times and resets them. |
| 963 | `let line = str(floor(quad_count)) + " quads \|";` | Starts the line with the quad count. |
| 964 | `for (let i = 0; i < len(PHASE_NAMES); i = i + 1) {` | For each phase: |
| 965 | `let ms = floor(phase_times[i] / frames * 10000) / 10;` | Average milliseconds, to one decimal place: seconds per frame x 1000, kept to tenths. |
| 966 | `line = line + " " + PHASE_NAMES[i] + " " + str(ms) + " ms";` | Adds the phase's name and time to the line. |
| 967 | `phase_times[i] = 0;` | Resets it for the next period. |
| 968 | `}` | Ends the `for` loop from line 964. |
| 969 | `print line;` | Prints the line. |
| 970 | `}` | Ends `print_timings` (line 962). |

### Main: startup

| Line | Code | What it does |
|---:|---|---|
| 972 | `fn main() {` | The game's entry point. |
| 973 | `sdl_init("LunarCraft", WIN_W, WIN_H);` | Opens the 960 x 600 window. |
| 974 | `if (!load_atlas()) { print "Could not load atlas.png"; sdl_quit(); ret; }` | If the atlas is missing, prints an error, shuts SDL down, and stops. |
| 975 | `let t0 = sdl_time();` | Start time of world setup. Inside the frame loop, a different `t0` hides this one. |
| 976 | `generate_world();` | Creates the blocks. |
| 977 | `build_all_chunks();` | Builds every chunk's faces. |
| 978 | `init_render_buffers();` | Allocates the render buffers. |
| 979 | `init_clip_buffers();` | Allocates the clipping buffers. |
| 980 | `init_hud();` | Builds the hotbar icons. |
| 981 | `sdl_set_blend(true);` | Makes alpha work for the translucent HUD rectangles. |
| 982 | `print "world ready in " + str(floor((sdl_time() - t0) * 1000)) + " ms";` | Prints how long setup took, in milliseconds. |
| 983 | `spawn_player();` | Puts the player in the world. |
| 984 | `if (DEMO_SECONDS > 0) sdl_set_vsync(false);` | Benchmark mode: turns vsync off, to measure maximum speed. |
| 985 | `else sdl_set_relative_mouse(true);` | Normal play: captures the mouse, hiding the cursor. |
| 987 | `let start = sdl_time();` | Time the game started. |
| 988 | `let last = start;` | Time of the previous frame. |
| 989 | `let total_frames = 0;` | Frames drawn in total. |
| 990 | `let running = true;` | Set to false to end the loop. |
| 991 | `let next_action = 0;` | Earliest time the next mouse action may happen. |
| 992 | `let next_daylight = 0;` | Next time to update the light. |
| 993 | `let fps = 0;` | FPS shown on the HUD. |
| 994 | `let fps_frames = 0;` | Frames since the HUD number last updated. |
| 995 | `let fps_since = start;` | When it last updated. |
| 996 | `let timing_frames = 0;` | Frames since timings last printed. |
| 997 | `let timing_since = start;` | When they last printed. |

### Main: the frame loop

| Line | Code | What it does |
|---:|---|---|
| 999 | `while (running) {` | One pass of this loop draws one frame. |
| 1000 | `let t0 = sdl_time();` | Start of this frame. |
| 1001 | `if (sdl_poll() == "quit" \|\| sdl_key_down("escape")) running = false;` | Handles window events. Quits on window close or Esc. This call also collects mouse movement. |
| 1002 | `let dt = min(0.05, t0 - last);` | Seconds since the last frame, limited to 0.05 so a stall can't launch the player through a wall. |
| 1003 | `last = t0;` | Remembers this frame's time. |
| 1004 | `let now = t0 - start;` | Seconds since the game started. |
| 1006 | `if (DEMO_SECONDS > 0) {` | In benchmark mode: |
| 1007 | `yaw = 2.35 + now * 0.5;` | Spins the view at a steady rate. |
| 1008 | `step_player(dt, 0, 0, false);` | Keeps physics running but doesn't walk. |
| 1009 | `if (now > DEMO_SECONDS) running = false;` | Ends the run when time is up. |
| 1010 | `} else {` | In normal play: |
| 1011 | `look(sdl_mouse_dx(), sdl_mouse_dy());` | Turns the view by this frame's mouse movement. |
| 1012 | `let forward = 0;` | Forward input starts at 0. |
| 1013 | `let strafe = 0;` | Strafe input starts at 0. |
| 1014 | `if (sdl_key_down("w")) forward = forward + 1;` | W adds forward. |
| 1015 | `if (sdl_key_down("s")) forward = forward - 1;` | S subtracts, so holding both cancels out. |
| 1016 | `if (sdl_key_down("d")) strafe = strafe + 1;` | D adds strafe to the right. |
| 1017 | `if (sdl_key_down("a")) strafe = strafe - 1;` | A subtracts. |
| 1018 | `step_player(dt, forward, strafe, sdl_key_down("space"));` | Moves the player. Space requests a jump. |
| 1019 | `}` | Ends the `else` branch from line 1010. |
| 1020 | `sync_eye();` | Moves the camera to the player's new position. |
| 1021 | `update_camera();` | Recalculates the view vectors. |
| 1022 | `raycast();` | Finds the block under the crosshair. |
| 1024 | `if (DEMO_SECONDS == 0) {` | Input for blocks only applies in normal play: |
| 1025 | `for (let i = 0; i < SLOT_COUNT; i = i + 1) {` | For each number key: |
| 1026 | `if (sdl_key_down(SLOT_KEYS[i])) selected_block = i + 1;` | If it is down, selects that block. |
| 1027 | `}` | Ends the `for` loop from line 1025. |
| 1028 | `let wants_break = sdl_mouse_down(1);` | Is the left button held? |
| 1029 | `let wants_place = sdl_mouse_down(3);` | Is the right button held? |
| 1030 | `if (!wants_break && !wants_place) next_action = 0;` | If neither is held, clears the wait, so a fresh click always works at once. |
| 1031 | `if ((wants_break \|\| wants_place) && t0 >= next_action) {` | If a button is held and the wait is over: |
| 1032 | `if (wants_break) break_block(); else place_block();` | Breaks a block, or places one. Left wins if both are held. |
| 1033 | `next_action = t0 + ACTION_REPEAT;` | Starts the wait before the next repeat. |
| 1034 | `raycast();` | Aims again, because the world just changed and the highlight must match. |
| 1035 | `}` | Ends the `if` block from line 1031. |
| 1036 | `}` | Ends the `if` block from line 1024. |
| 1037 | `if (now >= next_daylight) {` | Once the next light update is due: |
| 1038 | `update_daylight(now);` | Updates light and sky. This first runs on frame one, which is what fills the color table. |
| 1039 | `next_daylight = now + 1;` | Schedules the next one in 1 second. |
| 1040 | `}` | Ends the `if` block from line 1037. |
| 1042 | `let t1 = sdl_time();` | Timestamp: input and physics are done. |
| 1043 | `project_world();` | Builds the quads. |
| 1044 | `let t2 = sdl_time();` | Timestamp: projection is done. |
| 1045 | `sort_quads();` | Orders the quads. |
| 1046 | `emit_highlight();` | Adds the highlight on top. |
| 1047 | `let t3 = sdl_time();` | Timestamp: sorting is done. |
| 1048 | `sdl_clear(sky_r, sky_g, sky_b);` | Clears the window to the sky color. |
| 1049 | `sdl_draw_tris(atlas, verts, quad_count * 4, indices, quad_count * 6);` | Draws the world in one call: 4 vertices and 6 indices per quad. |
| 1050 | `draw_hud(fps);` | Draws the HUD over it. |
| 1051 | `let t4 = sdl_time();` | Timestamp: drawing is submitted. |
| 1052 | `sdl_present();` | Shows the finished frame. With vsync on, this also waits for the display. |
| 1053 | `let t5 = sdl_time();` | Timestamp: the frame is finished. |
| 1055 | `phase_times[0] = phase_times[0] + t1 - t0;` | Adds time for the input and physics phase. |
| 1056 | `phase_times[1] = phase_times[1] + t2 - t1;` | Adds time for the project phase. |
| 1057 | `phase_times[2] = phase_times[2] + t3 - t2;` | Adds time for the sort phase. |
| 1058 | `phase_times[3] = phase_times[3] + t4 - t3;` | Adds time for the draw phase. |
| 1059 | `phase_times[4] = phase_times[4] + t5 - t4;` | Adds time for the present phase. |
| 1061 | `total_frames = total_frames + 1;` | Counts the frame overall. |
| 1062 | `fps_frames = fps_frames + 1;` | Counts it for the HUD number. |
| 1063 | `timing_frames = timing_frames + 1;` | Counts it for the printed timings. |
| 1064 | `if (t5 - fps_since >= 0.5) {` | Every half second: |
| 1065 | `fps = fps_frames / (t5 - fps_since);` | Calculates FPS: frames divided by elapsed time. |
| 1066 | `fps_frames = 0;` | Resets the frame count. |
| 1067 | `fps_since = t5;` | Restarts the timer. |
| 1068 | `}` | Ends the `if` block from line 1064. |
| 1069 | `if (t5 - timing_since >= 2) {` | Every 2 seconds: |
| 1070 | `print_timings(timing_frames);` | Prints the timings. |
| 1071 | `timing_frames = 0;` | Resets the frame count. |
| 1072 | `timing_since = t5;` | Restarts the timer. |
| 1073 | `}` | Ends the `if` block from line 1069. |
| 1074 | `}` | Ends the `while` loop from line 999. |
| 1075 | `print "average FPS: " + str(floor(total_frames / (sdl_time() - start)));` | After the loop ends, prints the average FPS. |
| 1076 | `sdl_set_relative_mouse(false);` | Gives the mouse back. |
| 1077 | `sdl_quit();` | Closes the window and cleans up. |
| 1078 | `}` | Ends `main` (line 972). |

### End of the file

| Line | Code | What it does |
|---:|---|---|
| 1080 | `main();` | Starts the game. This is the last line inside `game()`, so everything above it is already defined. |
| 1081 | `}` | Ends `game` (line 15). |
| 1083 | `game();` | Runs `game()`. Up to this point the file has only defined it, so this is where the program begins. |
