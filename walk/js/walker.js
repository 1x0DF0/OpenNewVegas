// Open New Vegas — first-person walking prototype.
//
// Renders walkable terrain in raw WebGL (no dependencies). Two sources:
//   default      the procedural Mojave (map/js/terrain.js), works instantly
//   ?src=x.json  a terrain bundle exported from the user's own game files
//                by engine/tools/worldexport (onv-terrain-1 schema)
//
// Coordinates: meters. +x = east, -z = north, +y = up.
// 1 map world-unit = 350 m (see docs/WORLDBUILDING.md).

(() => {
  const UNIT_M = 350;            // meters per map world-unit
  const EYE = 1.7;               // eye height
  const WALK = 5.5, SPRINT = 13; // m/s
  const GRAVITY = -16, JUMP = 5.5;
  const CHUNK = 256;             // chunk edge, meters
  const RES = 32;                // quads per chunk edge
  const VIEW_CHUNKS = 7;         // chunk radius kept around the player
  const FOG_END = 1650;

  const canvas = document.getElementById("gl");
  const gl = canvas.getContext("webgl", { antialias: true });
  if (!gl) { alert("WebGL is not available"); return; }

  // ── Height providers ─────────────────────────────────────────────────────
  // sample(xEast, zNorth) -> height in meters
  // Walking-scale detail on top of the broad procedural model (a few
  // non-repeating sine octaves; same function feeds mesh AND collision)
  function detail(x, n) {
    return 1.4 * Math.sin(x * 0.041 + Math.sin(n * 0.027)) *
                 Math.sin(n * 0.037 + Math.sin(x * 0.023)) +
           0.5 * Math.sin(x * 0.13 + n * 0.094) +
           0.22 * Math.sin(x * 0.31 - n * 0.27);
  }

  const proceduralProvider = {
    name: "procedural Mojave (no game data)",
    water: Terrain.WATER_LEVEL,
    sample: (x, n) => Terrain.elevation(x / UNIT_M, n / UNIT_M) + detail(x, n),
  };

  function bundleProvider(b) {
    const { originX, originY, spacing, width, height, heights } = b;
    const noData = b.noData ?? -1000;
    let lo = Infinity;
    for (const v of heights) if (v !== noData && v < lo) lo = v;
    const get = (i, j) => {
      const v = heights[Math.min(height - 1, Math.max(0, j)) * width +
                        Math.min(width - 1, Math.max(0, i))];
      return v === noData ? lo : v;
    };
    return {
      name: `bundle: ${b.worldspace || b.source} (${width}x${height})`,
      water: b.waterLevel ?? null,
      center: [originX + (width - 1) * spacing / 2,
               originY + (height - 1) * spacing / 2],
      sample(x, n) {
        const fx = (x - originX) / spacing, fy = (n - originY) / spacing;
        const i = Math.floor(fx), j = Math.floor(fy);
        const tx = fx - i, ty = fy - j;
        const a = get(i, j), c = get(i + 1, j);
        const d = get(i, j + 1), e = get(i + 1, j + 1);
        return (a + (c - a) * tx) * (1 - ty) + (d + (e - d) * tx) * ty;
      },
    };
  }

  let provider = proceduralProvider;

  // ── Terrain coloring (same palette as the atlas) ─────────────────────────
  const STOPS = [
    [330, [196, 174, 132]], [600, [186, 154, 110]], [950, [158, 124, 88]],
    [1400, [126, 102, 80]], [1950, [110, 104, 98]], [2400, [235, 236, 240]],
  ];
  function elevColor(e) {
    if (e <= STOPS[0][0]) return STOPS[0][1];
    for (let i = 0; i < STOPS.length - 1; i++) {
      const [e0, c0] = STOPS[i], [e1, c1] = STOPS[i + 1];
      if (e <= e1) {
        const t = (e - e0) / (e1 - e0);
        return [c0[0] + (c1[0] - c0[0]) * t, c0[1] + (c1[1] - c0[1]) * t,
                c0[2] + (c1[2] - c0[2]) * t];
      }
    }
    return STOPS[STOPS.length - 1][1];
  }

  // ── Tiny matrix helpers (column-major mat4) ──────────────────────────────
  function perspective(fovY, aspect, near, far) {
    const f = 1 / Math.tan(fovY / 2), nf = 1 / (near - far);
    return [f / aspect, 0, 0, 0, 0, f, 0, 0,
            0, 0, (far + near) * nf, -1, 0, 0, 2 * far * near * nf, 0];
  }
  function viewMatrix(px, py, pz, yaw, pitch) {
    // R = rotX(-pitch) * rotY(-yaw), then translate by -p
    const cy = Math.cos(yaw), sy = Math.sin(yaw);
    const cp = Math.cos(pitch), sp = Math.sin(pitch);
    const r = [cy, sy * sp, sy * cp, 0,
               0, cp, -sp, 0,
               -sy, cy * sp, cy * cp, 0,
               0, 0, 0, 1];
    r[12] = -(r[0] * px + r[4] * py + r[8] * pz);
    r[13] = -(r[1] * px + r[5] * py + r[9] * pz);
    r[14] = -(r[2] * px + r[6] * py + r[10] * pz);
    return r;
  }
  function mul(a, b) {
    const o = new Array(16);
    for (let c = 0; c < 4; c++)
      for (let r = 0; r < 4; r++)
        o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] +
                       a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
    return o;
  }

  // ── Shaders ──────────────────────────────────────────────────────────────
  function makeProgram(vsSrc, fsSrc) {
    const compile = (type, src) => {
      const s = gl.createShader(type);
      gl.shaderSource(s, src);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS))
        throw new Error(gl.getShaderInfoLog(s));
      return s;
    };
    const p = gl.createProgram();
    gl.attachShader(p, compile(gl.VERTEX_SHADER, vsSrc));
    gl.attachShader(p, compile(gl.FRAGMENT_SHADER, fsSrc));
    gl.linkProgram(p);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS))
      throw new Error(gl.getProgramInfoLog(p));
    return p;
  }

  const SKY = [0.72, 0.78, 0.84];

  const terrainProg = makeProgram(`
    attribute vec3 aPos; attribute vec3 aNorm; attribute vec3 aColor;
    uniform mat4 uMVP; uniform vec3 uEye;
    varying vec3 vColor; varying float vFog;
    void main() {
      gl_Position = uMVP * vec4(aPos, 1.0);
      vec3 sun = normalize(vec3(-0.55, 0.7, -0.45));
      float diff = max(dot(normalize(aNorm), sun), 0.0);
      vColor = aColor * (0.45 + 0.65 * diff);
      float d = distance(aPos, uEye);
      vFog = clamp(d / ${FOG_END.toFixed(1)}, 0.0, 1.0);
      vFog = vFog * vFog;
    }`, `
    precision mediump float;
    varying vec3 vColor; varying float vFog;
    void main() {
      vec3 sky = vec3(${SKY.join(",")});
      gl_FragColor = vec4(mix(vColor, sky, vFog), 1.0);
    }`);

  const flatProg = makeProgram(`
    attribute vec3 aPos; attribute vec4 aColor;
    uniform mat4 uMVP; uniform vec3 uEye;
    varying vec4 vColor; varying float vFog;
    void main() {
      gl_Position = uMVP * vec4(aPos, 1.0);
      vColor = aColor;
      float d = distance(aPos, uEye);
      vFog = clamp(d / ${FOG_END.toFixed(1)}, 0.0, 1.0);
      vFog = vFog * vFog;
    }`, `
    precision mediump float;
    varying vec4 vColor; varying float vFog;
    void main() {
      vec3 sky = vec3(${SKY.join(",")});
      gl_FragColor = vec4(mix(vColor.rgb, sky, vFog), vColor.a);
    }`);

  // ── Shared chunk index buffer ────────────────────────────────────────────
  const indices = [];
  for (let j = 0; j < RES; j++)
    for (let i = 0; i < RES; i++) {
      const a = j * (RES + 1) + i, b = a + 1;
      const c = a + RES + 1, d = c + 1;
      indices.push(a, c, b, b, c, d);
    }
  const indexBuf = gl.createBuffer();
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuf);
  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(indices), gl.STATIC_DRAW);
  const INDEX_COUNT = indices.length;

  // ── Chunk management ─────────────────────────────────────────────────────
  const chunks = new Map(); // "cx,cz" -> { buf }
  const STRIDE = 9 * 4;     // pos(3) norm(3) color(3) floats

  function buildChunk(cx, cz) {
    const verts = new Float32Array((RES + 1) * (RES + 1) * 9);
    const step = CHUNK / RES, eps = 2.0;
    let k = 0;
    for (let j = 0; j <= RES; j++)
      for (let i = 0; i <= RES; i++) {
        const x = cx * CHUNK + i * step;
        const z = cz * CHUNK + j * step;
        const n = -z; // northing
        const h = provider.sample(x, n);
        // Normal from central differences of the height field
        const hx = provider.sample(x + eps, n) - provider.sample(x - eps, n);
        const hn = provider.sample(x, n + eps) - provider.sample(x, n - eps);
        let nx = -hx / (2 * eps), ny = 1, nz = hn / (2 * eps);
        const il = 1 / Math.hypot(nx, ny, nz);
        const [r, g, b] = elevColor(h);
        verts[k++] = x; verts[k++] = h; verts[k++] = z;
        verts[k++] = nx * il; verts[k++] = ny * il; verts[k++] = nz * il;
        verts[k++] = r / 255; verts[k++] = g / 255; verts[k++] = b / 255;
      }
    const buf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buf);
    gl.bufferData(gl.ARRAY_BUFFER, verts, gl.STATIC_DRAW);
    return { buf };
  }

  const pending = [];
  function updateChunks(px, pz, budget) {
    const ccx = Math.floor(px / CHUNK), ccz = Math.floor(pz / CHUNK);
    pending.length = 0;
    for (let dz = -VIEW_CHUNKS; dz <= VIEW_CHUNKS; dz++)
      for (let dx = -VIEW_CHUNKS; dx <= VIEW_CHUNKS; dx++) {
        if (dx * dx + dz * dz > VIEW_CHUNKS * VIEW_CHUNKS + 2) continue;
        const key = (ccx + dx) + "," + (ccz + dz);
        if (!chunks.has(key)) pending.push([dx * dx + dz * dz, ccx + dx, ccz + dz, key]);
      }
    pending.sort((a, b) => a[0] - b[0]);
    for (let i = 0; i < Math.min(budget, pending.length); i++) {
      const [, cx, cz, key] = pending[i];
      chunks.set(key, buildChunk(cx, cz));
    }
    // Drop far chunks
    for (const [key, ch] of chunks) {
      const [cx, cz] = key.split(",").map(Number);
      const dx = cx - ccx, dz = cz - ccz;
      if (dx * dx + dz * dz > (VIEW_CHUNKS + 2) ** 2) {
        gl.deleteBuffer(ch.buf);
        chunks.delete(key);
      }
    }
  }

  // ── Water + location beacons (flat-shader geometry) ──────────────────────
  const waterBuf = gl.createBuffer();
  function waterVerts(px, pz, level) {
    const S = FOG_END * 1.5;
    const v = [];
    const quad = [[-S, -S], [S, -S], [-S, S], [S, -S], [S, S], [-S, S]];
    for (const [dx, dz] of quad)
      v.push(px + dx, level, pz + dz, 0.16, 0.33, 0.43, 0.62);
    return new Float32Array(v);
  }

  let beaconBuf = null, beaconCount = 0;
  function buildBeacons() {
    if (typeof LOCATIONS === "undefined" || provider !== proceduralProvider) {
      beaconCount = 0;
      return;
    }
    const v = [];
    for (const loc of LOCATIONS) {
      const x = loc.x * UNIT_M, z = -loc.y * UNIT_M;
      const h = provider.sample(x, loc.y * UNIT_M);
      v.push(x, h, z, 1.0, 0.76, 0.31, 0.9,
             x, h + 60, z, 1.0, 0.76, 0.31, 0.0);
    }
    beaconBuf = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, beaconBuf);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(v), gl.STATIC_DRAW);
    beaconCount = v.length / 7;
  }

  // ── Player state ─────────────────────────────────────────────────────────
  // Spawn at Goodsprings, looking northeast toward Vegas
  const player = {
    x: 23 * UNIT_M, z: -35 * UNIT_M, y: 0,
    yaw: 0.8, pitch: -0.04, vy: 0, grounded: true,
  };
  player.y = provider.sample(player.x, -player.z) + EYE;

  const keys = {};
  addEventListener("keydown", (e) => { keys[e.code] = true; });
  addEventListener("keyup", (e) => { keys[e.code] = false; });

  const overlay = document.getElementById("overlay");
  overlay.addEventListener("click", () => canvas.requestPointerLock());
  document.addEventListener("pointerlockchange", () => {
    overlay.style.display = document.pointerLockElement === canvas ? "none" : "flex";
  });
  addEventListener("mousemove", (e) => {
    if (document.pointerLockElement !== canvas) return;
    player.yaw += e.movementX * 0.0024;
    player.pitch = Math.max(-1.5, Math.min(1.5, player.pitch - e.movementY * 0.0024));
  });

  // ── HUD ──────────────────────────────────────────────────────────────────
  const posEl = document.getElementById("pos");
  const placeEl = document.getElementById("place");
  const compassEl = document.getElementById("compass");
  const fpsEl = document.getElementById("fps");
  document.getElementById("srcinfo").textContent = "terrain: " + provider.name;

  let lastPlace = "";
  function updateHud() {
    const wx = player.x / UNIT_M, wy = -player.z / UNIT_M;
    posEl.textContent =
      `world (${wx.toFixed(1)}, ${wy.toFixed(1)}) · ` +
      `${player.x.toFixed(0)} m E, ${(-player.z).toFixed(0)} m N · ` +
      `elev ${(player.y - EYE).toFixed(0)} m`;

    const dirs = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"];
    const idx = Math.round(((player.yaw % (2 * Math.PI)) + 2 * Math.PI) %
                           (2 * Math.PI) / (Math.PI / 4)) % 8;
    compassEl.textContent = "— " + dirs[idx] + " —";

    if (typeof LOCATIONS !== "undefined" && provider === proceduralProvider) {
      let best = null, bestD = 600;
      for (const loc of LOCATIONS) {
        const d = Math.hypot(loc.x * UNIT_M - player.x, -loc.y * UNIT_M - player.z);
        if (d < bestD) { best = loc; bestD = d; }
      }
      const name = best ? best.name : "";
      if (name !== lastPlace) {
        lastPlace = name;
        placeEl.textContent = name;
        placeEl.style.opacity = name ? 1 : 0;
      }
    }
  }

  // ── Main loop ────────────────────────────────────────────────────────────
  function resize() {
    canvas.width = innerWidth;
    canvas.height = innerHeight;
    gl.viewport(0, 0, canvas.width, canvas.height);
  }
  addEventListener("resize", resize);
  resize();

  gl.enable(gl.DEPTH_TEST);
  gl.enable(gl.CULL_FACE);

  // Pre-generate the spawn area so the first frame has ground
  updateChunks(player.x, player.z, 30);
  buildBeacons();

  let prev = performance.now(), fpsAcc = 0, fpsN = 0;

  function frame(now) {
    const dt = Math.min(0.05, (now - prev) / 1000);
    prev = now;

    // Movement
    const speed = keys.ShiftLeft || keys.ShiftRight ? SPRINT : WALK;
    let mx = 0, mz = 0;
    if (keys.KeyW) mz += 1;
    if (keys.KeyS) mz -= 1;
    if (keys.KeyA) mx -= 1;
    if (keys.KeyD) mx += 1;
    const ml = Math.hypot(mx, mz) || 1;
    const sy = Math.sin(player.yaw), cy = Math.cos(player.yaw);
    player.x += (mx / ml * cy + mz / ml * sy) * speed * dt;
    player.z += (mx / ml * sy - mz / ml * cy) * speed * dt;

    const ground = provider.sample(player.x, -player.z) + EYE;
    player.vy += GRAVITY * dt;
    if (keys.Space && player.grounded) { player.vy = JUMP; player.grounded = false; }
    player.y += player.vy * dt;
    if (player.y <= ground) { player.y = ground; player.vy = 0; player.grounded = true; }

    updateChunks(player.x, player.z, 4);

    // Render
    gl.clearColor(SKY[0], SKY[1], SKY[2], 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);

    const proj = perspective(1.15, canvas.width / canvas.height, 0.3, FOG_END * 1.6);
    const view = viewMatrix(player.x, player.y, player.z, player.yaw, player.pitch);
    const mvp = mul(proj, view);

    gl.useProgram(terrainProg);
    gl.uniformMatrix4fv(gl.getUniformLocation(terrainProg, "uMVP"), false, mvp);
    gl.uniform3f(gl.getUniformLocation(terrainProg, "uEye"),
                 player.x, player.y, player.z);
    const aPos = gl.getAttribLocation(terrainProg, "aPos");
    const aNorm = gl.getAttribLocation(terrainProg, "aNorm");
    const aColor = gl.getAttribLocation(terrainProg, "aColor");
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuf);
    for (const ch of chunks.values()) {
      gl.bindBuffer(gl.ARRAY_BUFFER, ch.buf);
      gl.enableVertexAttribArray(aPos);
      gl.vertexAttribPointer(aPos, 3, gl.FLOAT, false, STRIDE, 0);
      gl.enableVertexAttribArray(aNorm);
      gl.vertexAttribPointer(aNorm, 3, gl.FLOAT, false, STRIDE, 12);
      gl.enableVertexAttribArray(aColor);
      gl.vertexAttribPointer(aColor, 3, gl.FLOAT, false, STRIDE, 24);
      gl.drawElements(gl.TRIANGLES, INDEX_COUNT, gl.UNSIGNED_SHORT, 0);
    }

    // Flat-shaded extras: water plane + location beacons
    gl.useProgram(flatProg);
    gl.uniformMatrix4fv(gl.getUniformLocation(flatProg, "uMVP"), false, mvp);
    gl.uniform3f(gl.getUniformLocation(flatProg, "uEye"),
                 player.x, player.y, player.z);
    const fPos = gl.getAttribLocation(flatProg, "aPos");
    const fColor = gl.getAttribLocation(flatProg, "aColor");

    if (provider.water != null) {
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
      gl.bindBuffer(gl.ARRAY_BUFFER, waterBuf);
      gl.bufferData(gl.ARRAY_BUFFER,
                    waterVerts(player.x, player.z, provider.water), gl.DYNAMIC_DRAW);
      gl.enableVertexAttribArray(fPos);
      gl.vertexAttribPointer(fPos, 3, gl.FLOAT, false, 28, 0);
      gl.enableVertexAttribArray(fColor);
      gl.vertexAttribPointer(fColor, 4, gl.FLOAT, false, 28, 12);
      gl.disable(gl.CULL_FACE);
      gl.drawArrays(gl.TRIANGLES, 0, 6);
      gl.enable(gl.CULL_FACE);
      gl.disable(gl.BLEND);
    }

    if (beaconCount) {
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
      gl.bindBuffer(gl.ARRAY_BUFFER, beaconBuf);
      gl.enableVertexAttribArray(fPos);
      gl.vertexAttribPointer(fPos, 3, gl.FLOAT, false, 28, 0);
      gl.enableVertexAttribArray(fColor);
      gl.vertexAttribPointer(fColor, 4, gl.FLOAT, false, 28, 12);
      gl.drawArrays(gl.LINES, 0, beaconCount);
      gl.disable(gl.BLEND);
    }

    fpsAcc += dt; fpsN++;
    if (fpsAcc > 0.5) {
      fpsEl.textContent = (fpsN / fpsAcc).toFixed(0) + " fps · " +
                          chunks.size + " chunks";
      fpsAcc = 0; fpsN = 0;
    }
    updateHud();
    requestAnimationFrame(frame);
  }

  // ── Optional terrain bundle (?src=...) ───────────────────────────────────
  const params = new URLSearchParams(location.search);
  const src = params.get("src");

  function start() {
    if (params.has("ui") && params.get("ui") === "min")
      overlay.style.display = "none";
    requestAnimationFrame(frame);
  }

  if (src) {
    fetch(src)
      .then((r) => r.json())
      .then((b) => {
        if (b.format !== "onv-terrain-1") throw new Error("unknown bundle format");
        provider = bundleProvider(b);
        const [cx, cn] = provider.center;
        player.x = cx; player.z = -cn;
        player.y = provider.sample(cx, cn) + EYE;
        chunks.forEach((ch) => gl.deleteBuffer(ch.buf));
        chunks.clear();
        updateChunks(player.x, player.z, 30);
        buildBeacons();
        document.getElementById("srcinfo").textContent = "terrain: " + provider.name;
        start();
      })
      .catch((e) => {
        document.getElementById("srcinfo").textContent =
          "bundle load failed (" + e.message + ") — using procedural terrain";
        start();
      });
  } else {
    start();
  }
})();
