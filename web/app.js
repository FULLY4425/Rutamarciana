const cvs = document.getElementById("map");
const ctx = cvs.getContext("2d");
const cards = document.getElementById("cards");
const hint = document.getElementById("hint");
const mockflag = document.getElementById("mockflag");
const scalebarM = document.getElementById("scalebar-m");

const bg = new Image();
bg.src = "/preview.png";

const state = {
  revision: 0,
  region: new URLSearchParams(location.search).get("region") || "jezero",
  view: { k: 1, ox: 0, oy: 0 },
  grid: { rows: 500, cols: 500 },
  cellM: 4.0,
  cellXM: 4.0,
  start: null,
  end: null,
  stops: [],        // science waypoints in visit order (grid [row,col])
  data: null,       // last server response
  budgetMin: 240,
  sciencePerStop: 20,
  nStops: 0,
  viaServer: true,  // false when the mock fallback is used
};

function apiURL(path,region=state.region) {
  return path+(path.includes('?')?'&':'?')+'region='+encodeURIComponent(region);
}

const VIEW_K_MIN = 1, VIEW_K_MAX = 16;
const STATUS_LABEL = { cabe: "CABE ✓", justo: "JUSTO", no_cabe: "NO CABE ✗" };

function meters(a, b) {
  const dr = Math.abs(a[0] - b[0]) * state.cellM;
  const dc = Math.abs(a[1] - b[1]) * state.cellXM;
  return Math.hypot(dr, dc);
}

function fmtMin(s) { return (s / 60).toFixed(0); }
function fmtKm(m) { return (m / 1000).toFixed(2); }

function legCard(leg, label, extraCls) {
  if (!leg) return "";
  return `
    <div class="leg ${extraCls || ""}">
      <div class="leg-t">${label}</div>
      <div class="kv">${fmtMin(leg.time_s)} min · ${fmtKm(leg.distance_m)} km ·
        pendiente ≤ ${leg.slope_max_deg === null || leg.slope_max_deg === undefined ? "—" : leg.slope_max_deg.toFixed(1)}°</div>
    </div>`;
}

function contingencyLine(b) {
  const c = b && b.return_contingency;
  if (!c || !c.waypoint_returns || !c.waypoint_returns.length) return "";
  const wr = c.waypoint_returns;
  const bad = wr.some(w => w.distance_status === "exceeds_limit" || w.time_status === "exceeds_limit");
  const worstD = Math.max(...wr.map(w => w.distance_m));
  const worstT = Math.max(...wr.map(w => w.time_s));
  const cls = bad ? "bad" : "";
  const verdict = bad
    ? "RETORNO DE EMERGENCIA SOBRE EL LÍMITE"
    : "Retorno de emergencia dentro de 60 min / 2 km";
  return `<div class="kv cont ${cls}">${verdict} · peor parada ${fmtKm(worstD)} km / ${fmtMin(worstT)} min${c.status === "not_verified" && !bad ? " (modelo sin validación de campo)" : ""}</div>`;
}

// Budget breakdown: outbound + science + return + overhead = operational;
// reserve shown apart.
function budgetBreakdown(b) {
  if (!b || b.operational_s === undefined || b.operational_s === null) return "";
  const row = (label, s, cls) =>
    `<div class="brow ${cls || ""}"><span class="bl">${label}</span><i></i><span class="bv">${fmtMin(s)} min</span></div>`;
  let html = "";
  html += row("ida", b.outbound_travel_s ?? 0);
  html += row(`science · ${b.science_stops ?? 0} stop${(b.science_stops ?? 0) === 1 ? "" : "s"}`, b.science_total_s ?? 0);
  html += row("regreso", b.return_travel_s ?? 0);
  html += row("entrada + salida", b.overhead_s ?? 0);
  html += row("operacional", b.operational_s, "brow-total");
  if (b.reserve_fixed_s !== undefined && b.reserve_fixed_s !== null) {
    html += row("reserva · separada", b.reserve_fixed_s, "brow-res");
  }
  return `<div class="budget">${html}</div>`;
}

function stopList() {
  if (!state.stops.length) return "";
  return `<div class="kv stops-line">paradas en orden: ${state.stops
    .map((s, i) => `S${i + 1}(${s[0]},${s[1]})`)
    .join(" → ")}</div>`;
}

function renderCards() {
  cards.innerHTML = "";
  const fits = [];
  if (state.data) {
    const d = state.data;
    if (d.cell_m) state.cellM = d.cell_m;
    if (d.cell_x_m) state.cellXM = d.cell_x_m;
    const direct = state.start && state.end ? meters(state.start, state.end) : null;

    for (const name of ["safe", "fast"]) {
      const r = d[name];
      if (!r) continue;
      const b = r.budget || {};
      const status = b.status || "no_cabe";
      if (status === "cabe") fits.push(name);
      const statusCls = status === "cabe" ? "go" : status === "justo" ? "low" : "nogo";
      const legs = (r.outbound && r.outbound.legs) || [];
      const legHtml = legs.length
        ? legs.map((l, i) => {
            const from = i === 0 ? "A" : `S${i}`;
            const to = i === legs.length - 1 ? "B" : `S${i + 1}`;
            return `<div class="leg">
              <div class="leg-t">${from} → ${to}</div>
              <div class="kv">${fmtMin(l.time_s)} min · ${fmtKm(l.distance_m)} km</div>
            </div>`;
          }).join("")
        : "";
      const el = document.createElement("div");
      el.className = `card card--${name}`;
      el.innerHTML = `
        <div class="card-top">
          <h2>${name === "safe" ? "RUTA SAFE" : "RUTA FAST"}</h2>
          <span class="status ${statusCls}">${STATUS_LABEL[status] || status}</span>
        </div>
        <div class="big">${fmtMin(r.total_s)} min <span class="of">de ${fmtMin(state.budgetMin * 60)} min EVA</span></div>
        <div class="kv">route <b>${fmtKm(r.distance_m)} km</b>${direct !== null ? ` · recta A–B <b>${fmtKm(direct)} km</b> (plana)` : ""} ·
          rugosidad ${r.rough_mean === null || r.rough_mean === undefined ? "—" : r.rough_mean.toFixed(2)} ·
          dosis ${r.dose_msv === null || r.dose_msv === undefined ? "—" : r.dose_msv.toFixed(3)} mSv</div>
        ${stopList()}
        <div class="legs">
          ${legHtml || legCard(r.outbound, "IDA (outbound)")}
          ${legCard(r.return, "REGRESO (return)", "leg--return")}
        </div>
        ${budgetBreakdown(b)}
        ${contingencyLine(b)}
        <div class="verdict ${statusCls}">
          <span class="v-main">${STATUS_LABEL[status] || status}</span>
          <span class="v-sub">margen ${fmtMin(b.slack_s || 0)} min · límite: ${(b.limiting_terms || []).join(", ") || "—"}</span>
        </div>`;
      cards.appendChild(el);
    }
  }

  const reco = document.getElementById("reco");
  if (!state.data) { reco.textContent = "—"; reco.removeAttribute("data-state"); return; }
  if (!fits.length) {
    reco.textContent = "NINGUNA RUTA CABE bajo los supuestos del escenario";
    reco.dataset.state = "nogo";
  } else if (fits.length === 2) {
    reco.textContent = "AMBAS CABEN · SAFE penaliza pendiente y rugosidad";
    reco.dataset.state = "go";
  } else {
    reco.textContent = "SOLO CABE LA RUTA " + fits[0].toUpperCase() + "";
    reco.dataset.state = "mid";
  }
}

// grid → canvas coordinates, transformed by the current view (zoom + pan)
function g2c(pt) {
  const [r, c] = pt;
  const bx = (c / (state.grid.cols - 1)) * cvs.width;
  const by = (r / (state.grid.rows - 1)) * cvs.height;
  return [bx * state.view.k + state.view.ox, by * state.view.k + state.view.oy];
}

// client px → grid coordinates, inverse of g2c
function c2g(clientX, clientY) {
  const rect = cvs.getBoundingClientRect();
  const x = (clientX - rect.left) / rect.width * cvs.width;
  const y = (clientY - rect.top) / rect.height * cvs.height;
  const bx = (x - state.view.ox) / state.view.k;
  const by = (y - state.view.oy) / state.view.k;
  return [
    Math.max(0, Math.min(state.grid.rows - 1, Math.round(by / cvs.height * (state.grid.rows - 1)))),
    Math.max(0, Math.min(state.grid.cols - 1, Math.round(bx / cvs.width * (state.grid.cols - 1)))),
  ];
}

function clampView() {
  const v = state.view;
  v.k = Math.max(VIEW_K_MIN, Math.min(VIEW_K_MAX, v.k));
  if (v.k <= 1) { v.ox = 0; v.oy = 0; }
  else {
    const mx = cvs.width * (1 - v.k), my = cvs.height * (1 - v.k);
    v.ox = Math.max(mx, Math.min(0, v.ox));
    v.oy = Math.max(my, Math.min(0, v.oy));
  }
  if (scalebarM) scalebarM.textContent = Math.round((state.grid.cols - 1) * state.cellXM * 0.25 / v.k / 10) * 10 + " M";
}

function zoomAt(clientX, clientY, factor) {
  const rect = cvs.getBoundingClientRect();
  const mx = (clientX - rect.left) / rect.width * cvs.width;
  const my = (clientY - rect.top) / rect.height * cvs.height;
  const v = state.view;
  const k = Math.max(VIEW_K_MIN, Math.min(VIEW_K_MAX, v.k * factor));
  v.ox = mx - (mx - v.ox) * (k / v.k);
  v.oy = my - (my - v.oy) * (k / v.k);
  v.k = k;
  clampView();
  draw();
}

function setZoom(factor) {
  const rect = cvs.getBoundingClientRect();
  zoomAt(rect.left + rect.width / 2, rect.top + rect.height / 2, factor);
}

function resetView() {
  state.view = { k: 1, ox: 0, oy: 0 };
  clampView();
  draw();
}

// small legible tag with a dark plate so labels read over the orthofoto
function tag(text, x, y, color) {
  ctx.font = "bold 11px ui-monospace, Menlo, monospace";
  const w = ctx.measureText(text).width;
  ctx.fillStyle = "rgba(8, 10, 11, 0.85)";
  ctx.fillRect(x - 2, y - 12, w + 6, 15);
  ctx.fillStyle = color;
  ctx.fillText(text, x + 1, y);
}

function tracePath(pts) {
  ctx.beginPath();
  pts.forEach((p, i) => {
    const [x, y] = g2c(p);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });
}

// draw a route line: dark under-stroke first so it reads over the orthofoto
function drawRoute(pts, color, width, dash) {
  if (!pts || pts.length < 2) return;
  ctx.setLineDash(dash || []);
  ctx.strokeStyle = "rgba(7, 9, 9, 0.7)";
  ctx.lineWidth = width + 3;
  tracePath(pts);
  ctx.stroke();
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  tracePath(pts);
  ctx.stroke();
  ctx.setLineDash([]);
}

// direction chevrons along a path so return direction (B → A) is visible
function drawChevrons(pts, color) {
  if (!pts || pts.length < 8) return;
  const step = Math.max(6, Math.floor(pts.length / 14));
  ctx.strokeStyle = color;
  ctx.fillStyle = color;
  ctx.lineWidth = 2;
  for (let i = step; i < pts.length - 1; i += step) {
    const [x1, y1] = g2c(pts[i - 1]);
    const [x2, y2] = g2c(pts[i]);
    const [x3, y3] = g2c(pts[Math.min(i + 1, pts.length - 1)]);
    const ang = Math.atan2(y3 - y1, x3 - x1);
    ctx.save();
    ctx.translate(x2, y2);
    ctx.rotate(ang);
    ctx.beginPath();
    ctx.moveTo(-5, -5); ctx.lineTo(3, 0); ctx.lineTo(-5, 5);
    ctx.closePath();
    ctx.fill();
    ctx.restore();
  }
}

function draw2d() {
  ctx.clearRect(0, 0, cvs.width, cvs.height);
  ctx.save();
  if (bg.complete && bg.naturalWidth) {
    if (state.view.k === 1) ctx.drawImage(bg, 0, 0, cvs.width, cvs.height);
    else ctx.drawImage(bg, state.view.ox, state.view.oy, cvs.width * state.view.k, cvs.height * state.view.k);
  } else {
    ctx.fillStyle = "#0b0d0e";
    ctx.fillRect(0, 0, cvs.width, cvs.height);
  }
  ctx.restore();

  if (state.data) {
    for (const [name, color] of [["fast", "#f08a3c"], ["safe", "#f2c14e"]]) {
      const route = state.data[name];
      if (!route) continue;
      const isFast = name === "fast";
      // ida: solid (safe) or dashed (fast), thicker
      drawRoute(route.points, color, 3, isFast ? [10, 8] : []);
      // regreso: dotted, thinner, with direction chevrons toward A
      if (route.return && route.return.points) {
        drawRoute(route.return.points, color, 2, [1.5, 7]);
        drawChevrons(route.return.points, color);
      }
    }
  }

  state.stops.forEach((pt, i) => {
    const [x, y] = g2c(pt);
    ctx.beginPath();
    ctx.arc(x, y, 7, 0, Math.PI * 2);
    ctx.fillStyle = "#56d4c8";
    ctx.fill();
    ctx.strokeStyle = "#070909";
    ctx.lineWidth = 2;
    ctx.stroke();
    tag("S" + (i + 1), x + 10, y - 8, "#8fe6dd");
  });

  for (const [pt, color, label] of [
    [state.start, "#5fd48a", "A"],
    [state.end, "#e5606f", "B"],
  ]) {
    if (!pt) continue;
    const [x, y] = g2c(pt);
    ctx.beginPath();
    ctx.arc(x, y, 8, 0, Math.PI * 2);
    ctx.fillStyle = color;
    ctx.fill();
    ctx.strokeStyle = "#070909";
    ctx.lineWidth = 2;
    ctx.stroke();
    tag(label, x + 11, y - 9, color);
  }
}

function draw() { if (window.drawTerrain) window.drawTerrain(); else draw2d(); }

function nextHint() {
  if (!state.start) return "Selecciona A: punto de salida.";
  if (!state.end) return "Selecciona B: destino.";
  const need = state.nStops - state.stops.length;
  if (need > 0) return `Selecciona S${state.stops.length + 1} (${need} pendientes, en orden).`;
  return "Listo: pulsa CALCULAR RUTAS.";
}

async function compute() {
  if (!state.start || !state.end) {
    hint.textContent = "Selecciona primero salida y destino.";
    return;
  }
  if (state.stops.length !== state.nStops) { hint.textContent = nextHint(); return; }
  const revision = ++state.revision;
  hint.textContent = "Calculando…";
  try {
    const res = await fetch(apiURL("/api/route"), {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        schema_version: 3,
        start: state.start,
        end: state.end,
        science_waypoints: state.stops,
        scenario_assumptions: {
          suit_budget_s: state.budgetMin * 60,
          science_stop_s: state.sciencePerStop * 60,
        },
      }),
    });
    if (!res.ok) throw new Error(await res.text());
    const json = await res.json();
    if (revision !== state.revision) return;
    state.viaServer = true;
    if (json.grid && json.grid.rows) state.grid = json.grid;
    if (json.cell_m) state.cellM = json.cell_m;
    if (json.cell_x_m) state.cellXM = json.cell_x_m;
    state.data = json;
    mockflag.hidden = true;
    hint.textContent = "Rutas listas. Amarillo: SAFE · naranja: FAST · turquesa: regreso. Factibilidad de escenario.";
  } catch (err) {
    if (revision !== state.revision) return;
    state.revision++;
    state.data = null;
    mockflag.hidden = false;
    mockflag.textContent = "No se pudo calcular: " + err.message;
    hint.textContent = "Revisa los puntos y el servidor. No hay una ruta calculada.";
  }
  draw();
  renderCards();
}

function selectPoint(pt) {
  if (!state.start) {
    state.start = pt;
  } else if (!state.end) {
    state.end = pt;
  } else if (state.stops.length < state.nStops) {
    state.stops.push(pt);
  } else { return; }
  state.revision++;
  state.data = null;
  cards.innerHTML = "";
  mockflag.hidden = true;
  hint.textContent = nextHint();
  draw();
}

document.getElementById("zin").addEventListener("click", () => setZoom(1.3));
document.getElementById("zout").addEventListener("click", () => setZoom(1 / 1.3));
document.getElementById("zreset").addEventListener("click", resetView);

document.getElementById("compute").addEventListener("click", compute);

document.getElementById("reset").addEventListener("click", () => {
  state.start = null;
  state.end = null;
  state.stops = [];
  state.revision++;
  state.data = null;
  cards.innerHTML = "";
  mockflag.hidden = true;
  hint.textContent = nextHint();
  draw();
});

document.getElementById("budget").addEventListener("change", (e) => {
  state.budgetMin = Number(e.target.value);
  state.revision++;
  if (state.start && state.end && state.stops.length === state.nStops) compute();
});

document.getElementById("science").addEventListener("change", (e) => {
  state.sciencePerStop = Number(e.target.value);
  state.revision++;
  if (state.start && state.end && state.stops.length === state.nStops) compute();
});

document.getElementById("nstops").addEventListener("change", (e) => {
  state.nStops = Number(e.target.value);
  state.stops = state.stops.slice(0, state.nStops);
  if (state.nStops === 0) state.stops = [];
  state.revision++;
  state.data = null;
  cards.innerHTML = "";
  mockflag.hidden = true;
  hint.textContent = nextHint();
  draw();
});

bg.onload = draw;

(async function boot() {
  try {
    const revision=state.revision;
    const meta = await fetch(apiURL("/api/routes")).then((r) => r.json());
    if(revision!==state.revision)return;
    if (meta.grid && meta.grid.rows) state.grid = meta.grid;
    if (meta.cell_m) state.cellM = meta.cell_m;
    if (meta.cell_x_m) state.cellXM = meta.cell_x_m;
    if (meta.budget_s) state.budgetMin = Math.round(meta.budget_s / 60);
    if (meta.science_s) state.sciencePerStop = Math.round(meta.science_s / 60);
    document.getElementById("budget").value = String(state.budgetMin);
    document.getElementById("science").value = String(state.sciencePerStop);
    const r = meta.routes || meta;
    if (r.safe && r.fast) {
      state.start = r.safe.points[0];
      state.end = r.safe.points[r.safe.points.length - 1];
      state.stops = meta.science_waypoints || [];
      state.nStops = state.stops.length;
      document.getElementById("nstops").value = String(state.nStops);
      state.data = {...meta, safe: r.safe, fast: r.fast};
      draw();
      renderCards();
    }
  } catch (e) {
    /* sin datos: se queda esperando clics */
  }

  if (new URLSearchParams(location.search).get("demo") === "1") {
    state.start = [Math.round(state.grid.rows * 0.08), Math.round(state.grid.cols * 0.08)];
    state.end = [Math.round(state.grid.rows * 0.92), Math.round(state.grid.cols * 0.92)];
    state.stops = [
      [Math.round(state.grid.rows * 0.45), Math.round(state.grid.cols * 0.35)],
      [Math.round(state.grid.rows * 0.6), Math.round(state.grid.cols * 0.68)],
    ];
    state.nStops = 2;
    document.getElementById("nstops").value = "2";
    draw();
    await compute();
  }
})();
