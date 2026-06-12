// Open New Vegas — interactive world map viewer.
// Pan: drag · Zoom: scroll · Hover: details · Click list: travel

(() => {
  const canvas = document.getElementById("map");
  const ctx = canvas.getContext("2d");
  const tooltip = document.getElementById("tooltip");
  const locList = document.getElementById("loc-list");
  const searchBox = document.getElementById("search");
  const filtersEl = document.getElementById("filters");

  const TYPE_STYLE = {
    settlement: { color: "#e8c468", label: "Settlement" },
    district:   { color: "#f0a830", label: "Vegas District" },
    military:   { color: "#7fb069", label: "Military / Camp" },
    vault:      { color: "#5bc0eb", label: "Vault" },
    facility:   { color: "#b78fd6", label: "Facility" },
    landmark:   { color: "#e0e0e0", label: "Landmark" },
    ruin:       { color: "#a08a7a", label: "Ruin" },
    hazard:     { color: "#e05c5c", label: "Hazard" },
  };

  const W = Terrain.WORLD_SIZE;
  let terrain = null;

  // View state: world-space center + pixels per world unit
  const view = { cx: W / 2, cy: W / 2, scale: 7 };
  const enabledTypes = new Set(Object.keys(TYPE_STYLE));
  let searchTerm = "";
  let hovered = null;

  // ── Coordinate transforms ────────────────────────────────────────────────
  function w2s(wx, wy) {
    return [
      canvas.width / 2 + (wx - view.cx) * view.scale,
      canvas.height / 2 - (wy - view.cy) * view.scale,
    ];
  }
  function s2w(sx, sy) {
    return [
      view.cx + (sx - canvas.width / 2) / view.scale,
      view.cy - (sy - canvas.height / 2) / view.scale,
    ];
  }

  function visibleLocations() {
    return LOCATIONS.filter((l) => {
      if (!enabledTypes.has(l.type)) return false;
      if (searchTerm && !l.name.toLowerCase().includes(searchTerm)) return false;
      return true;
    });
  }

  // ── Drawing ──────────────────────────────────────────────────────────────
  function draw() {
    ctx.fillStyle = "#11100d";
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    if (terrain) {
      const [sx, sy] = w2s(0, W); // world top-left corner
      const sizePx = W * view.scale;
      ctx.imageSmoothingEnabled = true;
      ctx.drawImage(terrain.canvas, sx, sy, sizePx, sizePx);
      // Vignette border
      ctx.strokeStyle = "rgba(0,0,0,0.55)";
      ctx.lineWidth = 3;
      ctx.strokeRect(sx, sy, sizePx, sizePx);
    }

    drawRoads();
    drawRegions();
    drawLocations();
  }

  function drawRoads() {
    for (const road of ROADS) {
      const isHwy = road.kind === "highway";
      ctx.beginPath();
      road.points.forEach(([x, y], i) => {
        const [sx, sy] = w2s(x, y);
        i === 0 ? ctx.moveTo(sx, sy) : ctx.lineTo(sx, sy);
      });
      ctx.strokeStyle = isHwy ? "rgba(40,34,26,0.85)" : "rgba(50,44,36,0.65)";
      ctx.lineWidth = Math.max(1, (isHwy ? 0.45 : 0.28) * view.scale);
      ctx.lineJoin = "round";
      ctx.lineCap = "round";
      ctx.stroke();
      if (isHwy) {
        ctx.strokeStyle = "rgba(214,196,160,0.55)";
        ctx.lineWidth = Math.max(0.5, 0.06 * view.scale);
        ctx.setLineDash([0.8 * view.scale, 0.8 * view.scale]);
        ctx.stroke();
        ctx.setLineDash([]);
      }
    }
  }

  function drawRegions() {
    if (view.scale < 4.5) return;
    ctx.font = `italic ${Math.min(15, 1.6 * view.scale)}px Georgia, serif`;
    ctx.textAlign = "center";
    ctx.fillStyle = "rgba(60,50,38,0.7)";
    for (const r of REGIONS) {
      const [sx, sy] = w2s(r.x, r.y);
      ctx.fillText(r.name, sx, sy);
    }
  }

  function drawLocations() {
    const locs = visibleLocations();
    const r = Math.max(3.5, Math.min(7, view.scale * 0.7));
    const showAllLabels = view.scale > 10;

    for (const loc of locs) {
      const [sx, sy] = w2s(loc.x, loc.y);
      if (sx < -30 || sy < -30 || sx > canvas.width + 30 || sy > canvas.height + 30) continue;
      const style = TYPE_STYLE[loc.type];
      const isHover = hovered && hovered.id === loc.id;

      ctx.beginPath();
      ctx.arc(sx, sy, isHover ? r * 1.5 : r, 0, Math.PI * 2);
      ctx.fillStyle = style.color;
      ctx.fill();
      ctx.lineWidth = 1.5;
      ctx.strokeStyle = "rgba(0,0,0,0.8)";
      ctx.stroke();

      const major = loc.type === "settlement" || loc.type === "district" || loc.id === "hoover-dam";
      if (isHover || showAllLabels || (major && view.scale > 5.5)) {
        ctx.font = `bold ${isHover ? 13 : 11}px "Segoe UI", sans-serif`;
        ctx.textAlign = "left";
        const text = loc.name;
        const tw = ctx.measureText(text).width;
        ctx.fillStyle = "rgba(10,9,7,0.7)";
        ctx.fillRect(sx + r + 3, sy - 8, tw + 8, 16);
        ctx.fillStyle = isHover ? "#ffe9b0" : "#e8ddc8";
        ctx.fillText(text, sx + r + 7, sy + 4);
      }
    }
  }

  // ── Sidebar ──────────────────────────────────────────────────────────────
  function buildFilters() {
    for (const [type, style] of Object.entries(TYPE_STYLE)) {
      const label = document.createElement("label");
      label.className = "filter";
      label.innerHTML =
        `<input type="checkbox" checked data-type="${type}">` +
        `<span class="dot" style="background:${style.color}"></span>${style.label}`;
      label.querySelector("input").addEventListener("change", (e) => {
        e.target.checked ? enabledTypes.add(type) : enabledTypes.delete(type);
        buildList();
        draw();
      });
      filtersEl.appendChild(label);
    }
  }

  function buildList() {
    locList.innerHTML = "";
    const locs = visibleLocations().slice().sort((a, b) => a.name.localeCompare(b.name));
    for (const loc of locs) {
      const li = document.createElement("li");
      li.innerHTML =
        `<span class="dot" style="background:${TYPE_STYLE[loc.type].color}"></span>` +
        `${loc.name} <em>${loc.faction !== "None" ? loc.faction : ""}</em>`;
      li.addEventListener("click", () => {
        view.cx = loc.x; view.cy = loc.y;
        view.scale = Math.max(view.scale, 14);
        hovered = loc;
        draw();
      });
      li.addEventListener("mouseenter", () => { hovered = loc; draw(); });
      li.addEventListener("mouseleave", () => { hovered = null; draw(); });
      locList.appendChild(li);
    }
    document.getElementById("loc-count").textContent = `${locs.length} locations`;
  }

  // ── Input ────────────────────────────────────────────────────────────────
  let dragging = false, lastX = 0, lastY = 0, moved = false;

  canvas.addEventListener("mousedown", (e) => {
    dragging = true; moved = false;
    lastX = e.offsetX; lastY = e.offsetY;
  });
  window.addEventListener("mouseup", () => { dragging = false; });

  canvas.addEventListener("mousemove", (e) => {
    if (dragging) {
      view.cx -= (e.offsetX - lastX) / view.scale;
      view.cy += (e.offsetY - lastY) / view.scale;
      lastX = e.offsetX; lastY = e.offsetY;
      moved = true;
      tooltip.style.display = "none";
      draw();
      return;
    }
    // Hover hit-test
    let best = null, bestD = 14;
    for (const loc of visibleLocations()) {
      const [sx, sy] = w2s(loc.x, loc.y);
      const d = Math.hypot(sx - e.offsetX, sy - e.offsetY);
      if (d < bestD) { best = loc; bestD = d; }
    }
    if (best !== hovered) { hovered = best; draw(); }
    if (best) {
      tooltip.style.display = "block";
      tooltip.style.left = `${e.clientX + 16}px`;
      tooltip.style.top = `${e.clientY + 12}px`;
      tooltip.innerHTML =
        `<h3>${best.name}</h3>` +
        `<div class="meta">${TYPE_STYLE[best.type].label}` +
        (best.faction !== "None" ? ` · ${best.faction}` : "") +
        (best.real ? ` · <span class="real">real Mojave location</span>` : "") +
        `</div><p>${best.desc}</p>`;
      canvas.style.cursor = "pointer";
    } else {
      tooltip.style.display = "none";
      canvas.style.cursor = dragging ? "grabbing" : "grab";
    }
  });

  canvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    const factor = e.deltaY < 0 ? 1.15 : 1 / 1.15;
    const [wx, wy] = s2w(e.offsetX, e.offsetY);
    view.scale = Math.max(3, Math.min(60, view.scale * factor));
    // Keep the world point under the cursor fixed
    view.cx = wx - (e.offsetX - canvas.width / 2) / view.scale;
    view.cy = wy + (e.offsetY - canvas.height / 2) / view.scale;
    draw();
  }, { passive: false });

  searchBox.addEventListener("input", () => {
    searchTerm = searchBox.value.trim().toLowerCase();
    buildList();
    draw();
  });

  // ── Boot ─────────────────────────────────────────────────────────────────
  function resize() {
    const rect = canvas.parentElement.getBoundingClientRect();
    canvas.width = rect.width;
    canvas.height = rect.height;
    draw();
  }
  window.addEventListener("resize", resize);

  buildFilters();
  buildList();
  resize();
  view.scale = Math.min(canvas.width, canvas.height) / W * 0.95;

  // Generate terrain after first paint so the UI appears immediately
  requestAnimationFrame(() => {
    setTimeout(() => {
      terrain = Terrain.render(1024);
      document.getElementById("loading").style.display = "none";
      draw();
    }, 30);
  });
})();
