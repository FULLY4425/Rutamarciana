const cvs = document.getElementById("map");
const ctx = cvs.getContext("2d");
const cards = document.getElementById("cards");
const hint = document.getElementById("hint");

const bg = new Image();
bg.src = "/preview.png";

const state = {
  grid: { rows: 500, cols: 500 },
  start: null,
  end: null,
  data: null,
  budgetH: 4,
  scienceMin: 10,
};

function budgetSeconds() {
  return state.budgetH * 3600 - state.scienceMin * 60;
}

function g2c(pt) {
  const [r, c] = pt;
  return [
    (c / (state.grid.cols - 1)) * cvs.width,
    (r / (state.grid.rows - 1)) * cvs.height,
  ];
}

function draw() {
  ctx.clearRect(0, 0, cvs.width, cvs.height);
  if (bg.complete && bg.naturalWidth) {
    ctx.drawImage(bg, 0, 0, cvs.width, cvs.height);
  } else {
    ctx.fillStyle = "#0b1018";
    ctx.fillRect(0, 0, cvs.width, cvs.height);
  }

  if (state.data) {
    for (const [name, color] of [["fast", "#f08a3c"], ["safe", "#f2c14e"]]) {
      const route = state.data[name];
      if (!route || !route.points || route.points.length < 2) continue;
      ctx.strokeStyle = color;
      ctx.lineWidth = name === "safe" ? 3 : 2;
      ctx.setLineDash(name === "fast" ? [10, 8] : []);
      ctx.beginPath();
      route.points.forEach((p, i) => {
        const [x, y] = g2c(p);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      });
      ctx.stroke();
      ctx.setLineDash([]);
    }
  }

  for (const [pt, color] of [[state.start, "#4bcc31"], [state.end, "#cc314b"]]) {
    if (!pt) continue;
    const [x, y] = g2c(pt);
    ctx.beginPath();
    ctx.arc(x, y, 9, 0, Math.PI * 2);
    ctx.fillStyle = color;
    ctx.fill();
    ctx.strokeStyle = "#05070b";
    ctx.lineWidth = 2;
    ctx.stroke();
  }
}

function verdict(route) {
  const margin = budgetSeconds() - route.time_s;
  return { margin, ok: margin > 0 };
}

function renderCards() {
  if (!state.data) return;
  cards.innerHTML = "";
  for (const name of ["safe", "fast"]) {
    const r = state.data[name];
    if (!r) continue;
    const v = verdict(r);
    const el = document.createElement("div");
    el.className = "card";
    el.innerHTML = `
      <h2>${name === "safe" ? "SAFE ROUTE" : "FAST ROUTE"}</h2>
      <div class="big">${(r.time_s / 60).toFixed(0)} min</div>
      <div class="kv">${(r.distance_m / 1000).toFixed(2)} km ·
        roughness ${r.rough_mean === null ? "—" : r.rough_mean.toFixed(2)}</div>
      <div class="verdict ${v.ok ? "go" : "nogo"}">
        ${v.ok ? "GO" : "NO-GO"} · margin ${(v.margin / 60).toFixed(0)} min
      </div>`;
    cards.appendChild(el);
  }
}

async function compute() {
  if (!state.start || !state.end) {
    hint.textContent = "Pick a start and an end point first.";
    return;
  }
  hint.textContent = "Computing…";
  try {
    const res = await fetch("/api/route", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ start: state.start, end: state.end }),
    });
    if (!res.ok) throw new Error(await res.text());
    const json = await res.json();
    if (json.grid && json.grid.rows) state.grid = json.grid;
    state.data = json;
    hint.textContent = "Routes computed. Orange = fast, yellow = safe.";
  } catch (err) {
    // Fallback: mock routes so the page never looks broken in a demo.
    const mock = await fetch("/mock_routes.json").then((r) => r.json());
    const m = mock.routes || {};
    state.data = { safe: m.safe, fast: m.fast };
    hint.textContent = "Using precomputed routes (server has no cost grids).";
  }
  draw();
  renderCards();
}

cvs.addEventListener("click", (ev) => {
  const rect = cvs.getBoundingClientRect();
  const x = (ev.clientX - rect.left) / rect.width;
  const y = (ev.clientY - rect.top) / rect.height;
  const pt = [
    Math.max(0, Math.min(state.grid.rows - 1, Math.round(y * (state.grid.rows - 1)))),
    Math.max(0, Math.min(state.grid.cols - 1, Math.round(x * (state.grid.cols - 1)))),
  ];
  if (!state.start || (state.start && state.end)) {
    state.start = pt;
    state.end = null;
    state.data = null;
    cards.innerHTML = "";
    hint.textContent = "Now pick the end point.";
  } else {
    state.end = pt;
    hint.textContent = "Press COMPUTE ROUTES.";
  }
  draw();
});

document.getElementById("compute").addEventListener("click", compute);

document.getElementById("reset").addEventListener("click", () => {
  state.start = null;
  state.end = null;
  state.data = null;
  cards.innerHTML = "";
  hint.textContent = "Click the start point, then the end point.";
  draw();
});

document.getElementById("budget").addEventListener("change", (e) => {
  state.budgetH = Number(e.target.value);
  renderCards();
});

document.getElementById("science").addEventListener("change", (e) => {
  state.scienceMin = Number(e.target.value);
  renderCards();
});

bg.onload = draw;

(async function boot() {
  try {
    const meta = await fetch("/api/routes").then((r) => r.json());
    if (meta.grid && meta.grid.rows) state.grid = meta.grid;
    if (meta.budget_s) state.budgetH = Math.round(meta.budget_s / 3600);
    const r = meta.routes || meta;
    if (r.safe && r.fast) {
      state.data = { safe: r.safe, fast: r.fast };
      draw();
      renderCards();
    }
  } catch (e) {
    /* sin datos: se queda esperando clics */
  }

  if (new URLSearchParams(location.search).get("demo") === "1") {
    state.start = [Math.round(state.grid.rows * 0.08), Math.round(state.grid.cols * 0.08)];
    state.end = [Math.round(state.grid.rows * 0.92), Math.round(state.grid.cols * 0.92)];
    draw();
    await compute();
  }
})();
