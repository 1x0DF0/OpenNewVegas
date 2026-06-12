// Open New Vegas — engine-side terrain model.
//
// This is a faithful C++ port of map/js/terrain.js. The two implementations
// MUST stay in sync: elevation(x, y) is the single source of truth for the
// shape of the Mojave, shared by the web map viewer and the game engine.
// If you change a feature here, change it there (and vice versa).

#pragma once

namespace onv {

inline constexpr double WORLD_SIZE = 100.0;  // world units (1 unit ~ 350 m)
inline constexpr double WATER_LEVEL = 330.0; // meters

// Elevation in meters at world coordinates (x east, y north), deterministic.
double elevation(double x, double y);

} // namespace onv
