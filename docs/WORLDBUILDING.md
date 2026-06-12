# Worldbuilding Guide

## Coordinate system

The world is a 100×100 square of *world units*:

- `x` — 0 at the western edge (Spring Mountains), 100 at the east (beyond the Colorado River)
- `y` — 0 at the southern edge (Nipton / state line), 100 at the north (Sheep Range)
- 1 world unit ≈ 350 m. The full map is roughly 35×35 km of Mojave.

Anchor points, so you can eyeball placement:

| Place | (x, y) |
|---|---|
| Goodsprings | (23, 35) |
| Primm | (25, 13) |
| New Vegas Strip | (53, 64) |
| Hoover Dam | (85, 52) |
| Novac | (65, 18) |
| Jacobstown (Mt. Charleston) | (12, 77) |

## Terrain model

`map/js/terrain.js` builds elevation (in meters) as a sum of analytic features
over a seeded fractal noise base:

1. **Ridges** — polylines with height + width (Spring Mountains, McCullough
   Range, Eldorado Mountains, River Mountains, Sheep Range)
2. **Peaks** — gaussian bumps (Mt. Charleston, Black Mountain, Frenchman Mountain)
3. **Basins** — gaussian depressions (Las Vegas valley, Ivanpah dry lake,
   Eldorado valley)
4. **Water carves** — the Colorado River channel and Lake Mead, cut below the
   global water level (330 m)

Elevation is a *pure function* of `(x, y)` and the seed, so terrain can be
re-generated identically anywhere (viewer, exporter, game engine).

## Adding a location

Append an entry to `map/js/data/locations.js`:

```js
{
  id: "my-place",            // unique, kebab-case
  name: "My Place",
  type: "settlement",        // settlement | district | military | vault |
                             // facility | landmark | ruin | hazard
  faction: "Independent",    // who controls it (or "None")
  x: 50, y: 50,              // world coordinates
  real: true,                // is this a real Mojave location?
  desc: "One or two original sentences. Write your own copy — never paste " +
        "text from any game or wiki."
}
```

Rules:

- **Original prose only.** Descriptions must be written from scratch.
- Real-world places (most of the Mojave) get `real: true`.
- Keep placement consistent with the road network and terrain — a town in the
  middle of Lake Mead will be rejected in review.

## Roads

`map/js/data/roads.js` — each road is a named polyline of world-space points
with a `kind` of `"highway"` or `"road"`. Highways render wider and brighter.
