// Holophonor — the page plays the instrument. The paper, the brush and the
// projection are browser ports of src/paper.cpp and src/inkpainter.cpp.
"use strict";

// ?poster draws every animation in its finished state (for still images);
// reduced motion gets the same, so nothing on the page insists on moving.
const poster = new URLSearchParams(location.search).has("poster");
const reduceMotion = poster || matchMedia("(prefers-reduced-motion: reduce)").matches;
if (poster) document.documentElement.classList.add("poster");
const INK = [23, 21, 20];
const PAPER = "#f1ebde";

function makeRandom(seed) {
  // mulberry32: small, fast, repeatable.
  return () => {
    seed |= 0;
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function sizeCanvas(canvas, width, height, maxPixels = 2.6e6) {
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const scale = Math.min(dpr, Math.sqrt(maxPixels / (width * height)));
  canvas.width = Math.round(width * scale);
  canvas.height = Math.round(height * scale);
  return scale;
}

// ── Raw xuan paper ─────────────────────────────────────────────────────

function paintPaper(canvas) {
  const scale = sizeCanvas(canvas, innerWidth, innerHeight);
  const ctx = canvas.getContext("2d");
  const w = canvas.width, h = canvas.height;
  const random = makeRandom(7);
  ctx.fillStyle = PAPER;
  ctx.fillRect(0, 0, w, h);

  // Mottling: a tiny random image scaled up smoothly is value noise for free.
  for (const [cell, alpha] of [[220, 0.09], [48, 0.06]]) {
    const small = document.createElement("canvas");
    small.width = Math.max(2, Math.ceil(w / (cell * scale)) + 2);
    small.height = Math.max(2, Math.ceil(h / (cell * scale)) + 2);
    const sctx = small.getContext("2d");
    const data = sctx.createImageData(small.width, small.height);
    for (let i = 0; i < data.data.length; i += 4) {
      const v = 128 + (random() - 0.5) * 255;
      data.data[i] = data.data[i + 1] = data.data[i + 2] = v;
      data.data[i + 3] = 255;
    }
    sctx.putImageData(data, 0, 0);
    ctx.save();
    ctx.globalCompositeOperation = "overlay";
    ctx.globalAlpha = alpha;
    ctx.imageSmoothingQuality = "high";
    ctx.drawImage(small, 0, 0, w, h);
    ctx.restore();
  }

  // Laid lines from the papermaker's bamboo screen, barely there.
  ctx.strokeStyle = "rgba(120,100,70,0.028)";
  ctx.lineWidth = Math.max(1, scale);
  for (let y = 0; y < h; y += 9 * scale) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(w, y);
    ctx.stroke();
  }

  // Fibres: short curved strands, most lighter than the sheet.
  const fibres = (w * h) / (900 * scale * scale);
  ctx.lineCap = "round";
  for (let i = 0; i < fibres; i++) {
    const x = random() * w, y = random() * h;
    const length = (8 + random() * 46) * scale;
    const angle = random() * Math.PI * 2;
    const dx = Math.cos(angle), dy = Math.sin(angle);
    const bend = (random() * 2 - 1) * length * 0.2;
    const dark = random() < 1 / 9;
    ctx.strokeStyle = dark
      ? `rgba(90,72,50,${0.03 + random() * 0.04})`
      : `rgba(255,253,246,${0.13 + random() * 0.15})`;
    ctx.lineWidth = (0.4 + random() * 0.6) * scale;
    ctx.beginPath();
    ctx.moveTo(x, y);
    ctx.quadraticCurveTo(x + dx * length * 0.5 - dy * bend, y + dy * length * 0.5 + dx * bend, x + dx * length, y + dy * length);
    ctx.stroke();
  }

  // Grain.
  const image = ctx.getImageData(0, 0, w, h);
  const px = image.data;
  for (let i = 0; i < px.length; i += 4) {
    const d = (random() * 7 - 3.5) | 0;
    px[i] += d;
    px[i + 1] += d;
    px[i + 2] += d;
  }
  ctx.putImageData(image, 0, 0);

  const vignette = ctx.createRadialGradient(w / 2, h / 2, Math.hypot(w, h) * 0.31, w / 2, h / 2, Math.hypot(w, h) / 2);
  vignette.addColorStop(0, "rgba(110,86,52,0)");
  vignette.addColorStop(1, "rgba(110,86,52,0.11)");
  ctx.fillStyle = vignette;
  ctx.fillRect(0, 0, w, h);
}

// ── The performance: the app's demo, as an Aerophone sends it ───────────

const PHRASES = [
  { peak: 0.8, notes: [[62, 0.0, 0.7, false, 0], [74, 0.7, 1.25, true, 0], [72, 1.25, 1.55, true, 0], [69, 1.55, 1.85, true, 0], [67, 1.85, 2.15, true, 0], [69, 2.15, 3.9, true, 0.22]] },
  { peak: 0.85, notes: [[76, 4.4, 4.62, false, 0], [76, 4.7, 4.92, false, 0], [74, 5.0, 5.22, false, 0], [72, 5.3, 5.52, false, 0], [69, 5.6, 5.95, false, 0], [67, 6.05, 7.9, false, 0.25]] },
  { peak: 0.9, notes: [[57, 8.4, 9.0, false, 0], [69, 9.0, 9.4, true, 0], [81, 9.4, 10.6, true, 0.18], [74, 10.6, 11.0, true, 0], [62, 11.0, 13.0, true, 0.2]] },
];
const PERFORMANCE_END = 13.0;

// Breath, pitch and vibrato at time t, the way the synth would hear them.
function perform(t) {
  for (const phrase of PHRASES) {
    const notes = phrase.notes;
    const start = notes[0][1], end = notes[notes.length - 1][2];
    if (t < start - 0.001 || t > end + 0.3) continue;
    const u = (t - start) / (end - start);
    let breath = phrase.peak * (0.7 + 0.3 * Math.sin(Math.PI * Math.min(Math.max(u, 0), 1)));
    breath *= Math.min(1, Math.max(0, (t - start) / 0.035));
    breath *= Math.min(1, Math.max(0, (end - t) / 0.3));
    let sounding = null;
    for (let i = 0; i < notes.length; i++) {
      const [pitch, on, off, slurred, vibrato] = notes[i];
      if (!slurred && i > 0) {
        const gapStart = notes[i - 1][2];
        if (t > gapStart - 0.01 && t < on + 0.03) breath *= t < on ? 0 : (t - on) / 0.03;
      }
      if (t >= on && t < off) sounding = { pitch, on, vibrato };
    }
    if (!sounding) return { breath: 0, pitch: null };
    let bend = 0;
    if (sounding.vibrato > 0 && t > sounding.on + 0.35) {
      const ramp = Math.min(1, (t - sounding.on - 0.35) / 0.5);
      bend = -sounding.vibrato * ramp * (0.5 - 0.5 * Math.cos(2 * Math.PI * 5.2 * (t - sounding.on - 0.35)));
    }
    return { breath: Math.min(1, Math.max(0, breath)), pitch: sounding.pitch + bend, note: sounding.pitch, newNoteAt: sounding.on };
  }
  return { breath: 0, pitch: null };
}

const NAMES = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"];
const noteName = (n) => NAMES[n % 12] + (Math.floor(n / 12) - 1);

// ── The brush (src/inkpainter.cpp, Sumi) ────────────────────────────────

function brushTip(dry, seed) {
  const size = 128;
  const random = makeRandom(seed);
  const bristles = [];
  const a = random() * 6.28, b = random() * 6.28;
  for (let y = 0; y < size; y++) {
    const smooth = 0.5 + 0.25 * Math.sin(y * 0.41 + a) + 0.25 * Math.sin(y * 0.97 + b);
    const v = 0.6 * smooth + 0.4 * random();
    bristles.push(dry ? (v > 0.52 ? 1 : 0.06) : 0.5 + 0.5 * v);
  }
  const e1 = random() * 6.28, e2 = random() * 6.28;
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = size;
  const ctx = canvas.getContext("2d");
  const img = ctx.createImageData(size, size);
  const smoothstep = (e0, e1_, x) => {
    const t = Math.min(1, Math.max(0, (x - e0) / (e1_ - e0)));
    return t * t * (3 - 2 * t);
  };
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const dx = (x - size / 2 + 0.5) / (size / 2), dy = (y - size / 2 + 0.5) / (size / 2);
      const r = Math.hypot(dx, dy);
      const angle = Math.atan2(dy, dx);
      const edge = 0.86 + 0.07 * Math.sin(angle * 5 + e1) + 0.05 * Math.sin(angle * 13 + e2);
      const rim = dry ? 1 : smoothstep(0.35, 0.9, r);
      const alpha = (1 - smoothstep(edge - 0.12, edge, r)) * (1 - rim + rim * bristles[y]);
      const i = (y * size + x) * 4;
      img.data[i] = INK[0];
      img.data[i + 1] = INK[1];
      img.data[i + 2] = INK[2];
      img.data[i + 3] = Math.round(Math.min(1, Math.max(0, alpha)) * 255);
    }
  }
  ctx.putImageData(img, 0, 0);
  return canvas;
}

function startSheet() {
  const hero = document.querySelector(".hero");
  const canvas = document.getElementById("ink");
  const ctx = canvas.getContext("2d");
  const tip = brushTip(false, 3), dryTip = brushTip(true, 5);
  const ui = { note: document.getElementById("t-note"), cents: document.getElementById("t-cents"), breath: document.getElementById("t-breath") };
  let scale = 1, W = 0, H = 0;

  const resize = () => {
    const rect = hero.getBoundingClientRect();
    scale = sizeCanvas(canvas, rect.width, rect.height);
    W = canvas.width;
    H = canvas.height;
  };
  resize();

  const state = { t: -0.6, x: 0, penY: 0, prev: null, stroke: false, load: 1, carry: 0, silence: 9, sealed: false, fading: 0, rest: 0 };
  // Sized from the sheet's shorter measure, so a tall phone gets a fine brush.
  const measure = () => Math.min(H, W * 0.62);
  const yFor = (pitch) => H * 0.9 - measure() * 0.3 * Math.min(1, Math.max(0, (pitch - 55) / 28));
  const radiusFor = (level) => measure() * (0.0022 + 0.026 * Math.pow(level, 1.3));

  function stamp(at, radius, opacity, angle, dry) {
    ctx.save();
    ctx.translate(at.x, at.y);
    ctx.rotate(angle);
    ctx.globalAlpha = opacity;
    ctx.drawImage(dry ? dryTip : tip, -radius, -radius, radius * 2, radius * 2);
    ctx.restore();
  }

  function brush(from, to, level) {
    const length = Math.hypot(to.x - from.x, to.y - from.y);
    const radius = radiusFor(level);
    const spacing = Math.max(0.5 * scale, radius * 0.12);
    const angle = Math.atan2(to.y - from.y, to.x - from.x);
    let at = spacing - state.carry;
    while (at <= length) {
      const k = length > 0 ? at / length : 1;
      state.load = Math.max(0, state.load - spacing / (H * 0.7));
      const dry = state.load < 0.3;
      const opacity = (0.08 + 0.1 * level) * (0.5 + 0.5 * state.load);
      stamp({ x: from.x + (to.x - from.x) * k, y: from.y + (to.y - from.y) * k + (Math.random() - 0.5) * 0.4 * scale }, radius * (0.94 + 0.12 * Math.random()), opacity, angle, dry);
      if (!dry && Math.random() < 0.06) stamp({ x: from.x + (to.x - from.x) * k, y: from.y + (to.y - from.y) * k }, radius * 1.7, 0.018, angle, false);
      at += spacing;
    }
    state.carry = length - (at - spacing);
  }

  function seal() {
    const size = measure() * 0.05;
    const x = Math.min(state.x + size * 0.6, W - size * 1.8);
    const y = Math.min(Math.max(state.penY + size * 0.9, H * 0.55), H * 0.92 - size);
    ctx.save();
    ctx.translate(x + size / 2, y + size / 2);
    ctx.rotate((-2.5 * Math.PI) / 180);
    ctx.globalAlpha = 0.86;
    ctx.fillStyle = "#b8442e";
    ctx.beginPath();
    ctx.roundRect(-size / 2, -size / 2, size, size, size * 0.08);
    ctx.fill();
    ctx.fillStyle = PAPER;
    ctx.font = `900 ${Math.round(size * 0.72)}px "Noto Serif CJK JP", "Noto Serif CJK SC", "Hiragino Mincho ProN", "Songti SC", "SimSun", serif`;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText("音", 0, size * 0.04);
    ctx.globalAlpha = 0.5;
    for (let i = 0; i < 70; i++) {
      ctx.beginPath();
      ctx.arc(-size / 2 + Math.random() * size, -size / 2 + Math.random() * size, (0.3 + Math.random() * 0.9) * scale, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();
  }

  function step(dt) {
    state.t += dt;
    const p = perform(state.t);
    const sounding = p.pitch !== null && p.breath > 0.012;
    // A little faster than the app, so one performance fills the sheet.
    const margin = W * 0.07, speed = W / 17;
    if (state.x < margin) state.x = margin;

    if (sounding) {
      const target = yFor(p.pitch);
      state.penY = state.stroke ? state.penY + (target - state.penY) * (1 - Math.exp(-dt / 0.12)) : target;
      if (!state.stroke) {
        if (state.prev) state.x += Math.min(state.silence, 1.6) * speed * 0.55 + 5 * scale;
        state.stroke = true;
        state.load = 1;
        state.carry = 0;
        state.prev = { x: state.x, y: state.penY };
        stamp(state.prev, radiusFor(p.breath) * 1.1, 0.3, 0, false);
      }
      state.x += speed * dt;
      const to = { x: state.x, y: state.penY };
      brush(state.prev, to, p.breath);
      state.prev = to;
      state.silence = 0;
      const cents = Math.round((p.pitch - Math.round(p.pitch)) * 100);
      ui.note.textContent = noteName(Math.round(p.pitch));
      ui.cents.textContent = (cents < 0 ? "−" : "+") + String(Math.abs(cents)).padStart(2, "0") + "¢";
    } else {
      state.stroke = false;
      state.silence += dt;
    }
    ui.breath.style.width = `${Math.round(p.breath * 104)}px`;

    if (!sounding && state.t > PERFORMANCE_END && !state.sealed && state.silence > 1.4) {
      seal();
      state.sealed = true;
    }
    // After a pause, the sheet absorbs the ink to a ghost and the brush begins again.
    if (state.sealed && state.t > PERFORMANCE_END + 6) {
      state.fading += dt;
      ctx.save();
      ctx.globalCompositeOperation = "destination-out";
      ctx.globalAlpha = Math.min(1, dt * 2.4);
      ctx.fillRect(0, 0, W, H);
      ctx.restore();
      if (state.fading > 1.1) Object.assign(state, { t: -0.4, x: 0, prev: null, stroke: false, sealed: false, fading: 0, silence: 9 });
    }
  }

  if (reduceMotion) {
    // Paint the whole performance once, without motion.
    for (let i = 0; i < (PERFORMANCE_END + 2) * 60; i++) step(1 / 60);
    return;
  }

  let visible = true, last = performance.now();
  new IntersectionObserver(([entry]) => { visible = entry.isIntersecting; last = performance.now(); }).observe(hero);
  const frame = (now) => {
    const dt = Math.min(0.05, (now - last) / 1000);
    last = now;
    if (visible) step(dt);
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);

  let resizeTimer = 0;
  addEventListener("resize", () => {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(() => {
      const old = document.createElement("canvas");
      old.width = canvas.width;
      old.height = canvas.height;
      old.getContext("2d").drawImage(canvas, 0, 0);
      const ox = state.x / W, oy = state.penY / H;
      resize();
      ctx.drawImage(old, 0, 0, W, H);
      state.x = ox * W;
      state.penY = oy * H;
      if (state.prev) state.prev = { x: state.x, y: state.penY };
    }, 150);
  });
}

// ── The projection (src/inkpainter.cpp, Projection) ─────────────────────

const INTERVALS = [
  ["unison", [1, 1]], ["perfect fifth", [3, 2]], ["major third", [5, 4]], ["perfect fourth", [4, 3]],
  ["major sixth", [5, 3]], ["octave", [2, 1]], ["minor seventh", [9, 5]], ["tritone", [7, 5]], ["minor third", [6, 5]],
];

function startProjection() {
  const section = document.getElementById("projection");
  const canvas = document.getElementById("harmonograph");
  const ctx = canvas.getContext("2d");
  const nameEl = document.getElementById("interval-name"), ratioEl = document.getElementById("interval-ratio");
  let W = 0, H = 0, scale = 1;
  const resize = () => {
    const rect = section.getBoundingClientRect();
    scale = sizeCanvas(canvas, rect.width, rect.height, 1.3e6); // soft light needs few pixels
    W = canvas.width;
    H = canvas.height;
  };
  resize();
  addEventListener("resize", resize);

  let t = 0, phase = 0, index = 1, held = 0, glow = 0;
  const trail = [];
  const figure = (a, b, ph, radius, cx, cy, tilt) => {
    const path = new Path2D();
    const points = 640, turns = 3, damping = Math.log(4) / (turns * 2 * Math.PI);
    for (let i = 0; i <= points; i++) {
      const s = (i / points) * turns * 2 * Math.PI;
      const r = radius * Math.exp(-damping * s);
      const x = r * Math.sin(a * s + ph), y = r * 0.92 * Math.sin(b * s + ph * 0.5);
      const px = cx + x * Math.cos(tilt) - y * Math.sin(tilt), py = cy + x * Math.sin(tilt) + y * Math.cos(tilt);
      if (i === 0) path.moveTo(px, py); else path.lineTo(px, py);
    }
    return path;
  };
  const show = () => {
    nameEl.textContent = INTERVALS[index][0];
    ratioEl.textContent = INTERVALS[index][1].join(" : ");
  };
  show();

  const draw = (dt) => {
    t += dt;
    held += dt;
    // Each interval is breathed in, held, and let go.
    const life = held / 4.2;
    const target = life < 0.12 ? life / 0.12 : life > 0.85 ? Math.max(0, (1 - life) / 0.15) : 1;
    glow += (target - glow) * Math.min(1, dt * 6);
    phase += dt * (0.35 + 0.25 * glow);
    if (held > 4.2) {
      held = 0;
      index = (index + 1) % INTERVALS.length;
      show();
    }
    const [a, b] = INTERVALS[index][1];
    // Beside the words on a wide screen, beneath them on a narrow one.
    const wide = W > H * 1.3;
    const cx = W * (wide ? 0.72 : 0.5), cy = H * (wide ? 0.5 : 0.68);
    const radius = Math.min(H * (wide ? 0.34 : 0.24), W * (wide ? 0.2 : 0.4)) * (0.35 + 0.65 * glow);
    trail.unshift(figure(a, b, phase, radius, cx, cy, phase * 0.15));
    if (trail.length > 3) trail.pop();

    ctx.clearRect(0, 0, W, H);
    ctx.globalCompositeOperation = "lighter";
    trail.forEach((path, age) => {
      const fade = Math.pow(0.45, age) * glow;
      // Brighter than the app: here the light falls on a transparent canvas
      // over the page, not on an opaque night ground.
      for (const [width, alpha] of [[4, 0.1], [1.2, 0.75]]) {
        ctx.lineWidth = width * scale;
        ctx.strokeStyle = `rgba(216,180,106,${alpha * fade})`;
        ctx.stroke(path);
      }
    });
    // A pilot light, as in the app, while the projector rests.
    if (glow < 0.05) {
      ctx.lineWidth = scale;
      ctx.strokeStyle = `rgba(216,180,106,${0.06 + 0.05 * Math.sin(t * 1.6)})`;
      ctx.beginPath();
      ctx.arc(cx, cy, H * 0.035, 0, Math.PI * 2);
      ctx.stroke();
    }
    ctx.globalCompositeOperation = "source-over";
  };

  if (reduceMotion) {
    // Settle on one figure, with its trail, at full light.
    for (let i = 0; i < 40; i++) draw(1 / 30);
    return;
  }
  let visible = false, last = performance.now();
  new IntersectionObserver(([entry]) => { visible = entry.isIntersecting; last = performance.now(); }).observe(section);
  const frame = (now) => {
    const dt = Math.min(0.05, (now - last) / 1000);
    last = now;
    if (visible) draw(dt);
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
}

// ── Plate III: one note, three breaths ─────────────────────────────────

const ALTO_D4 = {
  pp: [-29.0, -43.8, -66.8, -74.4, -74.9, -73.4, -80.5, -83.5, -88.3, -84.7, -86.6, -92.1, -98.0, -101.5, -98.2, -102.3, -108.1, -104.4, -105.7, -99.2, -101.8, -99.2, -103.1, -101.6, -104.7, -107.6, -106.8, -108.7, -106.5, -109.4, -107.3, -107.0],
  mf: [-20.0, -29.6, -52.6, -40.3, -31.4, -43.9, -45.2, -44.7, -44.8, -49.1, -51.0, -53.8, -59.2, -70.1, -59.8, -54.6, -57.3, -63.4, -67.8, -55.1, -58.8, -65.8, -65.2, -59.8, -66.1, -65.7, -65.4, -80.1, -71.7, -72.8, -67.8, -68.3],
  ff: [-14.3, -26.9, -38.0, -34.3, -32.4, -38.1, -39.9, -37.9, -51.4, -48.0, -43.8, -57.0, -55.5, -51.7, -55.7, -71.1, -71.2, -68.3, -57.3, -55.1, -58.6, -60.1, -60.4, -66.6, -71.3, -67.2, -59.9, -66.4, -71.4, -64.8, -71.7, -70.3],
};

function drawSpectrum() {
  const svg = document.getElementById("spectrum");
  const NS = "http://www.w3.org/2000/svg";
  const el = (name, attrs, text) => {
    const node = document.createElementNS(NS, name);
    for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, v);
    if (text !== undefined) node.textContent = text;
    svg.appendChild(node);
    return node;
  };
  const left = 52, right = 618, top = 40, bottom = 318;
  const x = (k) => left + ((k - 1) / 31) * (right - left);
  const y = (db) => top + ((-12 - db) / (112 - 12)) * (bottom - top);
  for (const db of [-20, -40, -60, -80, -100]) {
    el("line", { x1: left, x2: right, y1: y(db), y2: y(db), class: "spectrum-axis" });
    el("text", { x: left - 10, y: y(db) + 3.5, "text-anchor": "end", class: "spectrum-tick" }, db);
  }
  for (const k of [1, 4, 8, 12, 16, 20, 24, 28, 32]) el("text", { x: x(k), y: bottom + 22, "text-anchor": "middle", class: "spectrum-tick" }, k);
  el("text", { x: right, y: bottom + 22, "text-anchor": "end", class: "spectrum-tick", dy: 14 }, "harmonic →");
  const layers = [
    ["pp", "rgba(27,25,23,0.32)", "4 4"],
    ["mf", "#8a6a3a", ""],
    ["ff", "#1b1917", ""],
  ];
  const paths = [];
  layers.forEach(([name, colour, dash], i) => {
    const d = ALTO_D4[name].map((db, k) => `${k ? "L" : "M"}${x(k + 1).toFixed(1)},${y(db).toFixed(1)}`).join("");
    paths.push(el("path", { d, class: "spectrum-line", stroke: colour, "stroke-dasharray": dash }));
    if (name === "ff") ALTO_D4.ff.forEach((db, k) => el("circle", { cx: x(k + 1), cy: y(db), r: 1.8, fill: colour }));
    const lx = 400 + i * 74;
    el("line", { x1: lx, x2: lx + 26, y1: 18, y2: 18, stroke: colour, "stroke-width": 1.6, "stroke-dasharray": dash });
    el("text", { x: lx + 32, y: 22, class: "spectrum-legend", fill: colour }, name);
  });
  // Draw the lines in, the first time the plate is seen.
  if (!reduceMotion) {
    for (const path of paths) {
      const length = path.getTotalLength();
      path.style.strokeDasharray = path.getAttribute("stroke-dasharray") || `${length}`;
      if (!path.getAttribute("stroke-dasharray")) {
        path.style.strokeDashoffset = `${length}`;
        path.style.transition = "stroke-dashoffset 2.4s cubic-bezier(0.3, 0.6, 0.2, 1)";
      }
    }
    new IntersectionObserver((entries, observer) => {
      if (!entries[0].isIntersecting) return;
      paths.forEach((path) => (path.style.strokeDashoffset = "0"));
      observer.disconnect();
    }, { threshold: 0.4 }).observe(svg);
  }
}

// ── Voices ─────────────────────────────────────────────────────────────

function startVoices() {
  let current = null;
  for (const button of document.querySelectorAll(".voice")) {
    const bar = button.querySelector(".progress span");
    let audio = null;
    const tick = () => {
      if (!audio || audio.paused) return;
      bar.style.width = `${(audio.currentTime / audio.duration) * 100 || 0}%`;
      requestAnimationFrame(tick);
    };
    button.addEventListener("click", () => {
      if (!audio) {
        audio = new Audio(button.dataset.src);
        audio.addEventListener("ended", () => {
          button.classList.remove("playing");
          bar.style.width = "0";
        });
      }
      if (audio.paused) {
        if (current && current !== audio) current.pause();
        document.querySelectorAll(".voice.playing").forEach((b) => b.classList.remove("playing"));
        current = audio;
        audio.play();
        button.classList.add("playing");
        requestAnimationFrame(tick);
      } else {
        audio.pause();
        button.classList.remove("playing");
      }
    });
    button.setAttribute("aria-label", `Play ${button.querySelector(".voice-name").textContent}`);
  }
}

// ── Small things ───────────────────────────────────────────────────────

function startReveals() {
  if (poster) return;
  const targets = document.querySelectorAll(".chapter > *, .night-text > *, .colophon > *");
  const observer = new IntersectionObserver((entries) => {
    for (const entry of entries)
      if (entry.isIntersecting) {
        entry.target.classList.add("seen");
        observer.unobserve(entry.target);
      }
  }, { rootMargin: "0px 0px -8% 0px" });
  targets.forEach((t) => {
    t.classList.add("reveal");
    observer.observe(t);
  });
}

function startCopy() {
  for (const button of document.querySelectorAll(".copy"))
    button.addEventListener("click", async () => {
      try {
        await navigator.clipboard.writeText(button.dataset.copy);
        button.textContent = "copied";
      } catch {
        button.textContent = "select & copy";
      }
      setTimeout(() => (button.textContent = "copy"), 1600);
    });
}

const paperCanvas = document.getElementById("paper");
paintPaper(paperCanvas);
let paperTimer = 0, paperSize = [innerWidth, innerHeight];
addEventListener("resize", () => {
  clearTimeout(paperTimer);
  paperTimer = setTimeout(() => {
    // Phones resize as the address bar moves; only repaint for real changes.
    if (Math.abs(innerWidth - paperSize[0]) > 40 || Math.abs(innerHeight - paperSize[1]) > 160) {
      paperSize = [innerWidth, innerHeight];
      paintPaper(paperCanvas);
    }
  }, 200);
});
startSheet();
startProjection();
drawSpectrum();
startVoices();
startReveals();
startCopy();
