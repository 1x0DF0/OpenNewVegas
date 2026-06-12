// Open New Vegas — procedural Mojave terrain.
//
// Elevation (meters) is a pure function of (x, y) in world coordinates
// (100x100 units, x east, y north) and a fixed seed. The same model will
// later drive the engine-side heightmap export, so this file is the
// authoritative definition of the Mojave's shape.

const Terrain = (() => {
  const SEED = 0x57e6a5; // "Vegas"
  const WORLD_SIZE = 100;
  const WATER_LEVEL = 330; // meters — Colorado River / Lake Mead surface

  // ── Seeded noise ─────────────────────────────────────────────────────────
  function hash2(ix, iy) {
    let h = (ix * 374761393 + iy * 668265263 + SEED * 144665) | 0;
    h = Math.imul(h ^ (h >>> 13), 1274126177);
    h ^= h >>> 16;
    return (h >>> 0) / 4294967295;
  }

  function smooth(t) { return t * t * (3 - 2 * t); }

  function valueNoise(x, y) {
    const ix = Math.floor(x), iy = Math.floor(y);
    const fx = smooth(x - ix), fy = smooth(y - iy);
    const a = hash2(ix, iy), b = hash2(ix + 1, iy);
    const c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
  }

  // Fractal noise in [-1, 1]
  function fbm(x, y, octaves) {
    let sum = 0, amp = 0.5, freq = 1, norm = 0;
    for (let i = 0; i < octaves; i++) {
      sum += amp * (valueNoise(x * freq, y * freq) * 2 - 1);
      norm += amp;
      amp *= 0.5;
      freq *= 2.07;
    }
    return sum / norm;
  }

  // ── Geometry helpers ─────────────────────────────────────────────────────
  function distToSegment(px, py, ax, ay, bx, by) {
    const dx = bx - ax, dy = by - ay;
    const len2 = dx * dx + dy * dy;
    let t = len2 === 0 ? 0 : ((px - ax) * dx + (py - ay) * dy) / len2;
    t = Math.max(0, Math.min(1, t));
    const ex = ax + t * dx - px, ey = ay + t * dy - py;
    return Math.sqrt(ex * ex + ey * ey);
  }

  function distToPolyline(px, py, pts) {
    let d = Infinity;
    for (let i = 0; i < pts.length - 1; i++) {
      d = Math.min(d, distToSegment(px, py, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1]));
    }
    return d;
  }

  const gauss = (d, w) => Math.exp(-(d * d) / (w * w));

  // ── Mojave features ──────────────────────────────────────────────────────
  const RIDGES = [
    // Spring Mountains — the western wall
    { pts: [[6, 28], [9, 45], [11, 62], [13, 76], [16, 90]], h: 1700, w: 7 },
    // McCullough Range — southern border hills
    { pts: [[36, 1], [44, 9], [54, 12], [63, 7]], h: 850, w: 5 },
    // Eldorado Mountains — along the river, south of the Dam
    { pts: [[77, 4], [81, 18], [84, 32], [82, 42]], h: 800, w: 4 },
    // River Mountains — between Vegas and Lake Mead
    { pts: [[70, 42], [75, 50], [78, 56]], h: 650, w: 4 },
    // Sheep Range — northern horizon
    { pts: [[46, 90], [58, 95], [72, 97]], h: 1200, w: 6 },
    // Goodsprings / Bird Spring hills
    { pts: [[18, 36], [22, 40]], h: 450, w: 4 },
  ];

  const PEAKS = [
    { x: 12, y: 78, h: 1500, w: 5 },  // Mt. Charleston (on top of Spring Mtns ridge)
    { x: 45, y: 25, h: 750, w: 3.5 }, // Black Mountain
    { x: 66, y: 70, h: 850, w: 3 },   // Frenchman Mountain
    { x: 89, y: 60, h: 500, w: 3 },   // Fortification Hill
  ];

  const BASINS = [
    { x: 52, y: 66, d: 260, w: 14 },  // Las Vegas valley
    { x: 26, y: 13, d: 160, w: 6 },   // Ivanpah dry lake (Primm)
    { x: 27, y: 26, d: 110, w: 4 },   // Jean dry lake
    { x: 62, y: 38, d: 130, w: 8 },   // Eldorado valley
  ];

  // Colorado River channel — carved below water level
  const RIVER = [[87, 54], [89, 46], [87, 37], [90, 27], [88, 16], [91, 7], [90, -2]];

  // Lake Mead — a chain of overlapping carve blobs forming one body of
  // water north of the Dam, narrowing into the canyon above it
  const LAKE = [
    { x: 76, y: 70, w: 6 }, { x: 80, y: 67, w: 6 }, { x: 84, y: 64, w: 6 },
    { x: 88, y: 62, w: 5.5 }, { x: 91, y: 67, w: 5 }, { x: 86, y: 70, w: 5.5 },
    { x: 84, y: 75, w: 5 }, { x: 87, y: 57, w: 3.5 },
  ];

  // ── Elevation function ───────────────────────────────────────────────────
  function elevation(x, y) {
    // Desert floor: ~700 m with broad fractal variation
    let e = 700 + fbm(x * 0.045, y * 0.045, 5) * 260;

    for (const r of RIDGES) {
      const d = distToPolyline(x, y, r.pts);
      // Ridge profile with a little noise so crests aren't glassy
      e += r.h * gauss(d, r.w) * (0.8 + 0.4 * valueNoise(x * 0.3, y * 0.3));
    }
    for (const p of PEAKS) {
      const d = Math.hypot(x - p.x, y - p.y);
      e += p.h * gauss(d, p.w);
    }
    for (const b of BASINS) {
      const d = Math.hypot(x - b.x, y - b.y);
      e -= b.d * gauss(d, b.w);
    }

    // River carve: cut to ~250 m inside the channel, blend at banks
    const dr = distToPolyline(x, y, RIVER);
    if (dr < 6) {
      const t = gauss(dr, 1.6);
      e = e * (1 - t) + 250 * t;
    }
    // Lake Mead carve: cut to ~300 m
    for (const l of LAKE) {
      const d = Math.hypot(x - l.x, y - l.y);
      if (d < l.w * 2.2) {
        const t = gauss(d, l.w * 0.75);
        e = e * (1 - t) + 295 * t;
      }
    }
    return e;
  }

  // ── Rendering ────────────────────────────────────────────────────────────
  // Elevation → RGB, Mojave palette
  function colorize(e, slope) {
    let r, g, b;
    if (e < WATER_LEVEL) {
      const depth = Math.min(1, (WATER_LEVEL - e) / 90);
      r = 38 - 14 * depth; g = 84 - 30 * depth; b = 110 - 28 * depth;
      return [r, g, b];
    }
    const stops = [
      [330, [196, 174, 132]],  // shoreline sand
      [600, [186, 154, 110]],  // desert floor
      [950, [158, 124, 88]],   // scrubland
      [1400, [126, 102, 80]],  // rocky slopes
      [1950, [110, 104, 98]],  // high stone
      [2400, [235, 236, 240]], // Charleston snow
    ];
    let lo = stops[0], hi = stops[stops.length - 1];
    for (let i = 0; i < stops.length - 1; i++) {
      if (e >= stops[i][0] && e <= stops[i + 1][0]) { lo = stops[i]; hi = stops[i + 1]; break; }
    }
    const t = Math.max(0, Math.min(1, (e - lo[0]) / (hi[0] - lo[0] || 1)));
    r = lo[1][0] + (hi[1][0] - lo[1][0]) * t;
    g = lo[1][1] + (hi[1][1] - lo[1][1]) * t;
    b = lo[1][2] + (hi[1][2] - lo[1][2]) * t;
    // Hillshade
    const shade = 0.62 + 0.38 * slope;
    return [r * shade, g * shade, b * shade];
  }

  // Render the full world to an offscreen canvas of N x N pixels.
  // Row 0 is the NORTH edge (world y = 100).
  function render(N) {
    const heights = new Float32Array(N * N);
    const step = WORLD_SIZE / N;
    for (let j = 0; j < N; j++) {
      const wy = (1 - j / (N - 1)) * WORLD_SIZE;
      for (let i = 0; i < N; i++) {
        heights[j * N + i] = elevation(i / (N - 1) * WORLD_SIZE, wy);
      }
    }

    const canvas = document.createElement("canvas");
    canvas.width = N; canvas.height = N;
    const ctx = canvas.getContext("2d");
    const img = ctx.createImageData(N, N);
    const data = img.data;

    // Light from the northwest (screen upper-left)
    const lx = -0.62, ly = -0.62, lz = 0.48;
    const metersPerPx = step * 350; // 1 world unit ≈ 350 m

    for (let j = 0; j < N; j++) {
      for (let i = 0; i < N; i++) {
        const h = heights[j * N + i];
        const hr = heights[j * N + Math.min(N - 1, i + 1)];
        const hd = heights[Math.min(N - 1, j + 1) * N + i];
        // Surface normal from finite differences (exaggerate relief 2x)
        const dzx = (hr - h) / metersPerPx * 2;
        const dzy = (hd - h) / metersPerPx * 2;
        const inv = 1 / Math.sqrt(dzx * dzx + dzy * dzy + 1);
        const dot = Math.max(0, (-dzx * lx - dzy * ly + lz) * inv) / lz;
        const slope = Math.min(1.25, dot);

        const [r, g, b] = colorize(h, slope);
        const k = (j * N + i) * 4;
        data[k] = r; data[k + 1] = g; data[k + 2] = b; data[k + 3] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);
    return { canvas, heights, size: N };
  }

  return { elevation, render, WORLD_SIZE, WATER_LEVEL };
})();

if (typeof module !== "undefined") module.exports = { Terrain };
