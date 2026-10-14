// The Space page's picture, alive: it moves with the song's beat (Cubase's tempo and position) and with the
// effect's own knobs. The scene's own lights glow on each beat (a "highlights" layer cut from the picture),
// and every effect adds its own motion:
//   Reverb   lights bloom and fade with Decay (after the Pre-delay), ripples spread across the floor, dust floats
//   Delay    echoes run down the tunnel of arches at the delay Time, each fainter by Feedback (Ping-Pong: left / right)
//   Chorus   the picture shimmers at Rate / Depth, a light sweeps along the guitars
//   Phaser   rings swirl around the column at Rate, the light waves at the sides undulate
//   Hall     a wave of light rolls from the stage through the hall, the chandeliers sparkle
//   Room     short, punchy lamp flashes; the drum kit lights up on the kick and the snare
//   Special  the crystal pulses in changing colours, orbits spin, particles rise (faster with Shift)
// While the song is stopped it only breathes slowly.

import { graph, clamp } from "./ui.js";

// Where things are in each picture (0..1 of the picture's width / height).
const SPOTS = {
  reverb: { centre: [0.49, 0.72] },
  delay: { vanish: [0.78, 0.5], near: [0.2, 0.46] },
  chorus: { from: [0.24, 0.72], to: [0.68, 0.62] },
  phaser: { column: [0.5, 0.42] },
  hall: { stage: [0.5, 0.97], lights: [[0.505, 0.12], [0.505, 0.4]] },
  room: { kit: [0.31, 0.8], piano: [0.83, 0.78] },
  special: { cube: [0.49, 0.52] },
};

function rng(seed) {
  let s = seed >>> 0 || 1;
  return () => { s ^= s << 13; s ^= s >>> 17; s ^= s << 5; return ((s >>> 0) % 100000) / 100000; };
}

export function createScene(parent, x, y, w, h) {
  const g = graph(parent, x, y, w, h, "scene-canvas");
  g.wrap.style.borderRadius = "inherit";
  Object.assign(g.canvas.style, { position: "absolute", left: "0", top: "0", width: "100%", height: "100%", display: "block" });
  const S = {
    key: null, img: null, glow: null, glowKey: "", params: {}, on: false,
    beat: { bpm: 0, ppq: 0, bar: 0, num: 4, playing: false, at: 0 },
    level: -100, avg: -60, hit: 0, lastHitAt: -1e9,
    particles: [], raf: 0, shown: false, t0: performance.now(),
  };

  // ---- picture + its highlights (the lights that glow on the beat) --------------------------
  function fit() {
    const iw = S.img.naturalWidth, ih = S.img.naturalHeight;
    const s = Math.max(w / iw, h / ih);
    return { s, ox: (w - iw * s) / 2, oy: (h - ih * s) / 2, dw: iw * s, dh: ih * s };
  }
  const at = (f, [u, v]) => [f.ox + u * f.dw, f.oy + v * f.dh];

  function buildGlow() {
    const f = fit();
    const k = g.canvas.width / w;
    const c = document.createElement("canvas");
    c.width = Math.max(1, Math.round(w * k / 2));
    c.height = Math.max(1, Math.round(h * k / 2));
    const cx = c.getContext("2d");
    cx.drawImage(S.img, f.ox * c.width / w, f.oy * c.height / h, f.dw * c.width / w, f.dh * c.height / h);
    const d = cx.getImageData(0, 0, c.width, c.height), p = d.data;
    for (let i = 0; i < p.length; i += 4) {
      const l = (0.3 * p[i] + 0.59 * p[i + 1] + 0.11 * p[i + 2]) / 255;
      const a = Math.pow(clamp((l - 0.55) / 0.45, 0, 1), 1.6);
      p[i] *= a; p[i + 1] *= a; p[i + 2] *= a;
    }
    cx.putImageData(d, 0, 0);
    const out = document.createElement("canvas");
    out.width = c.width; out.height = c.height;
    const ox = out.getContext("2d");
    ox.filter = "blur(" + Math.max(2, c.width / 160) + "px)";
    ox.drawImage(c, 0, 0);
    ox.filter = "none";
    ox.globalCompositeOperation = "lighter";
    ox.drawImage(c, 0, 0);
    S.glow = out;
    S.glowKey = S.key + ":" + g.canvas.width;
  }

  function seedParticles() {
    const r = rng(S.key.length * 7919 + 13);
    const n = { reverb: 46, hall: 34, special: 70, chorus: 30, phaser: 26, delay: 18, room: 20 }[S.key] || 24;
    S.particles = Array.from({ length: n }, () => ({ x: r(), y: r(), z: 0.4 + 0.6 * r(), ph: r() * 6.28, sp: 0.4 + r() }));
  }

  // ---- the beat ---------------------------------------------------------------------------------
  /** Quarter notes since the song's start, now (extrapolated from the last frame). */
  function ppqNow(now) {
    const b = S.beat;
    if (!b.playing || b.bpm <= 0) return null;
    return b.ppq + ((now - b.at) / 60000) * b.bpm;
  }
  const beatSeconds = () => (S.beat.bpm > 0 ? 60 / S.beat.bpm : 0.6);
  /** Seconds since the last beat (or since the last hit heard, without a tempo), and which beat of the bar. */
  function sinceBeat(now) {
    const q = ppqNow(now);
    if (q === null) return { age: (now - S.lastHitAt) / 1000, index: 0, q: null };
    const index = Math.floor(q);
    return { age: (q - index) * beatSeconds(), index, q, inBar: Math.floor(q - S.beat.bar + 1e-6) % Math.max(1, S.beat.num) };
  }
  const live = () => S.beat.playing || S.level > -60;

  // ---- drawing ----------------------------------------------------------------------------------
  function frame(now) {
    S.raf = 0;
    if (!S.shown) return;
    schedule();
    if (now - (S.lastDraw || 0) < 1000 / 40) return;   // 40 pictures a second is plenty, and light inside Cubase
    S.lastDraw = now;
    if (!S.img || !S.img.complete || !S.img.naturalWidth) return;
    if (S.glowKey !== S.key + ":" + g.canvas.width) buildGlow();
    const ctx = g.ctx;
    const t = (now - S.t0) / 1000;
    const f = fit();
    const p = S.params;
    const alive = live();
    const amount = alive ? clamp(0.45 + (p.mix ?? 25) / 45, 0.45, 1.5) : 0.25;
    const b = sinceBeat(now);
    const accent = b.inBar === 0 ? 1.25 : 1;

    // Slow drift, and a tiny push on every beat.
    const breathe = 1.012 + 0.012 * Math.sin(t / 7);
    const push = alive ? 0.004 * Math.exp(-b.age / 0.12) * accent : 0;
    const zoom = breathe + push;
    ctx.save();
    ctx.clearRect(0, 0, w, h);
    ctx.translate(w / 2, h / 2);
    ctx.scale(zoom, zoom);
    ctx.translate(-w / 2, -h / 2);

    if (S.key === "chorus" && alive) drawShimmer(ctx, f, t, p);
    else ctx.drawImage(S.img, f.ox, f.oy, f.dw, f.dh);

    // The picture's own lights glow with the beat.
    // Each beat's glow starts after the Pre-delay and fades with the effect's decay; until then the last
    // beat's glow carries on.
    const decay = Math.max(0.08, glowDecay(p));
    const pre = S.key === "reverb" || S.key === "hall" || S.key === "room" ? (p.pre ?? 0) / 1000 : 0;
    const since = b.age >= pre ? b.age - pre : b.age + beatSeconds() - pre;
    const env = alive ? Math.exp(-Math.max(0, since) / decay) : 0;
    const idle = 0.08 + 0.06 * Math.sin(t * 0.9);
    ctx.globalCompositeOperation = "lighter";
    ctx.globalAlpha = clamp((alive ? 0.12 : idle) + 0.75 * env * amount * accent, 0, 1);
    ctx.drawImage(S.glow, 0, 0, w, h);
    ctx.globalAlpha = 1;

    const fx = EFFECTS[S.key];
    if (fx) fx(ctx, f, t, b, env, amount, p, alive);
    ctx.globalCompositeOperation = "source-over";
    ctx.restore();

    // Vignette, so the motion stays inside the frame.
    const v = ctx.createRadialGradient(w / 2, h / 2, h * 0.35, w / 2, h / 2, w * 0.62);
    v.addColorStop(0, "rgba(0,0,0,0)");
    v.addColorStop(1, "rgba(0,0,0,0.35)");
    ctx.fillStyle = v;
    ctx.fillRect(0, 0, w, h);
  }

  function glowDecay(p) {
    switch (S.key) {
      case "reverb": return clamp((p.decay ?? 1.8) * 0.35, 0.15, 2.5);
      case "hall": return clamp((p.decay ?? 3) * 0.35, 0.3, 3.5);
      case "room": return clamp((p.decay ?? 0.7) * 0.25, 0.06, 0.6);
      case "delay": return 0.18;
      default: return 0.3;
    }
  }

  // Chorus: the picture itself wavers, in thin strips.
  function drawShimmer(ctx, f, t, p) {
    const rate = p.rate ?? 0.8, depth = (p.depth ?? 40) / 100;
    const strip = 3, iw = S.img.naturalWidth, ih = S.img.naturalHeight;
    for (let yy = 0; yy < h; yy += strip) {
      const v = (yy - f.oy) / f.dh;
      if (v < 0 || v > 1) continue;
      const dx = depth * 4 * Math.sin(2 * Math.PI * (rate * t) + yy / 28) + depth * 2 * Math.sin(2 * Math.PI * (rate * 1.7 * t) + yy / 11);
      ctx.drawImage(S.img, 0, v * ih, iw, (strip / f.dh) * ih, f.ox + dx, yy, f.dw, strip);
    }
  }

  function glowDot(ctx, x, y, r, colour, alpha) {
    if (alpha <= 0.003 || r <= 0.5) return;
    const gr = ctx.createRadialGradient(x, y, 0, x, y, r);
    gr.addColorStop(0, colour.replace("A", String(clamp(alpha, 0, 1))));
    gr.addColorStop(1, colour.replace("A", "0"));
    ctx.fillStyle = gr;
    ctx.fillRect(x - r, y - r, 2 * r, 2 * r);
  }

  const EFFECTS = {
    reverb(ctx, f, t, b, env, amount, p, alive) {
      const [cx, cy] = at(f, SPOTS.reverb.centre);
      // Ripples across the floor (flattened: the floor is seen at an angle).
      const decay = clamp((p.decay ?? 1.8) * 0.6, 0.4, 4);
      for (let k = 0; k < 3 && alive; k++) {
        const age = b.age + k * beatSeconds();
        const r = 30 + age * 260, a = 0.5 * amount * Math.exp(-age / decay);
        if (a < 0.01) continue;
        ctx.strokeStyle = `rgba(255,190,90,${a})`;
        ctx.lineWidth = 2.5;
        ctx.beginPath(); ctx.ellipse(cx, cy, r, r * 0.16, 0, 0, Math.PI * 2); ctx.stroke();
      }
      dust(ctx, f, t, alive ? 1 : 0.3, "rgba(255,210,140,A)", 0.04);
    },
    delay(ctx, f, t, b, env, amount, p, alive) {
      if (!alive) return;
      const [vx, vy] = at(f, SPOTS.delay.vanish), [nx, ny] = at(f, SPOTS.delay.near);
      const times = [0.125, 0.25, 1 / 3, 0.5, 0.75, 2 / 3, 1, 1.5, 2];   // the Time knob's note values, in beats
      const step = times[clamp(Math.round(p.time ?? 3), 0, 8)] * beatSeconds();
      const fb = clamp((p.feedback ?? 35) / 100, 0, 0.95), pingPong = (p.pingpong ?? 60) / 100;
      for (let i = 0; i < 9; i++) {
        const age = b.age - i * step;
        if (age < 0) break;
        const a = amount * Math.pow(fb, i) * Math.exp(-age / 0.35);
        if (a < 0.01) continue;
        const d = clamp(0.08 + i * 0.13 + age * 0.25, 0, 0.98);   // each echo sits further down the tunnel
        const side = (i % 2 ? 1 : -1) * pingPong * 0.07 * (1 - d);
        const x = nx + (vx - nx) * d + side * f.dw, y = ny + (vy - ny) * d;
        const aw = (1 - d) * f.dh * 0.55 + 4, ah = (1 - d) * f.dh * 0.85 + 6;
        ctx.save();
        ctx.shadowColor = "rgba(255,190,80,1)"; ctx.shadowBlur = 18;
        ctx.strokeStyle = `rgba(255,205,110,${clamp(a, 0, 1)})`; ctx.lineWidth = 3 * (1 - d) + 1;
        ctx.beginPath();
        ctx.moveTo(x - aw / 2, y + ah / 2);
        ctx.lineTo(x - aw / 2, y - ah / 2 + aw / 2);
        ctx.arc(x, y - ah / 2 + aw / 2, aw / 2, Math.PI, 0);
        ctx.lineTo(x + aw / 2, y + ah / 2);
        ctx.stroke();
        ctx.restore();
      }
    },
    chorus(ctx, f, t, b, env, amount, p, alive) {
      const [x0, y0] = at(f, SPOTS.chorus.from), [x1, y1] = at(f, SPOTS.chorus.to);
      const rate = p.rate ?? 0.8;
      const s = ((t * rate * 0.5) % 1.2) - 0.1;   // a light sweeping along the row of guitars
      const x = x0 + (x1 - x0) * s, y = y0 + (y1 - y0) * s;
      glowDot(ctx, x, y, 90, "rgba(255,200,110,A)", (alive ? 0.35 : 0.12) * amount);
      glowDot(ctx, x, y, 26, "rgba(255,240,200,A)", (alive ? 0.45 : 0.1) * amount);
      dust(ctx, f, t, alive ? 1 : 0.3, "rgba(255,215,150,A)", 0.03);
    },
    phaser(ctx, f, t, b, env, amount, p, alive) {
      const [cx, cy] = at(f, SPOTS.phaser.column);
      const rate = p.rate ?? 0.4, depth = (p.depth ?? 60) / 100;
      const spin = t * rate * Math.PI * 2 * (alive ? 1 : 0.2);
      for (let k = 0; k < 7; k++) {
        const yy = cy + (k - 3) * f.dh * 0.075, rx = f.dh * (0.42 - Math.abs(k - 3) * 0.035), ry = rx * 0.16;
        const a = (0.18 + 0.4 * env + 0.25 * (0.5 + 0.5 * Math.sin(spin + k))) * amount * (alive ? 1 : 0.4);
        ctx.strokeStyle = `rgba(255,200,95,${clamp(a, 0, 1)})`; ctx.lineWidth = 2.2;
        ctx.beginPath(); ctx.ellipse(cx, yy, rx, ry, 0, spin + k, spin + k + Math.PI * 1.1); ctx.stroke();
      }
      // Undulating light waves at both sides.
      for (const side of [-1, 1]) {
        for (let line = 0; line < 3; line++) {
          ctx.strokeStyle = `rgba(255,205,120,${clamp((0.22 + 0.3 * env) * amount, 0, 1)})`; ctx.lineWidth = 1.6;
          ctx.beginPath();
          for (let i = 0; i <= 60; i++) {
            const u = i / 60, xx = side < 0 ? u * (cx - f.dh * 0.45) : cx + f.dh * 0.45 + u * (w - cx - f.dh * 0.45);
            const yy = cy + f.dh * 0.15 + Math.sin(u * 9 + spin * 1.3 + line * 0.7) * f.dh * (0.05 + 0.08 * depth) * (side < 0 ? u : 1 - u);
            i ? ctx.lineTo(xx, yy) : ctx.moveTo(xx, yy);
          }
          ctx.stroke();
        }
      }
    },
    hall(ctx, f, t, b, env, amount, p, alive) {
      const [sx, sy] = at(f, SPOTS.hall.stage);
      if (alive) {   // a wave of light rolls from the stage up through the hall
        const decay = clamp((p.decay ?? 3.2) * 0.5, 0.6, 5);
        const r = 40 + b.age * 320, a = 0.4 * amount * Math.exp(-b.age / decay);
        const gr = ctx.createRadialGradient(sx, sy, Math.max(0, r - 90), sx, sy, r);
        gr.addColorStop(0, "rgba(255,190,100,0)"); gr.addColorStop(0.7, `rgba(255,200,120,${a})`); gr.addColorStop(1, "rgba(255,190,100,0)");
        ctx.fillStyle = gr; ctx.fillRect(0, 0, w, h);
      }
      const r = rng(Math.floor(t * (alive ? 9 : 3)) + 3);   // chandeliers sparkle
      for (const spot of SPOTS.hall.lights)
        for (let i = 0; i < 6; i++) {
          const [lx, ly] = at(f, spot);
          glowDot(ctx, lx + (r() - 0.5) * f.dh * 0.22, ly + (r() - 0.3) * f.dh * 0.12, 6 + r() * 7, "rgba(255,245,210,A)", (0.35 + env * 0.5) * amount * r());
        }
    },
    room(ctx, f, t, b, env, amount, p, alive) {
      if (!alive) return;
      const [kx, ky] = at(f, SPOTS.room.kit);
      const kick = b.inBar === undefined ? 1 : (b.inBar % 2 === 0 ? 1 : 0.55);   // kick on 1 and 3, snare on 2 and 4
      glowDot(ctx, kx, ky, f.dh * 0.32, "rgba(255,190,90,A)", 0.55 * env * amount * kick);
      glowDot(ctx, kx, ky - f.dh * 0.08, f.dh * 0.12, "rgba(255,240,200,A)", 0.5 * env * amount);
    },
    special(ctx, f, t, b, env, amount, p, alive) {
      const [cx, cy] = at(f, SPOTS.special.cube);
      const hue = (t * 40 + b.index * 47) % 360;
      glowDot(ctx, cx, cy, f.dh * 0.42, `hsla(${hue},90%,62%,A)`, (alive ? 0.2 + 0.55 * env : 0.12) * amount);
      const spin = t * (alive ? 0.9 : 0.25);
      for (let k = 0; k < 3; k++) {
        ctx.strokeStyle = `hsla(${(hue + k * 60) % 360},90%,70%,${clamp((0.25 + 0.4 * env) * amount, 0, 1)})`;
        ctx.lineWidth = 2;
        ctx.beginPath(); ctx.ellipse(cx, cy, f.dh * (0.36 + k * 0.06), f.dh * (0.07 + k * 0.02), 0.25 - k * 0.12, spin * (k % 2 ? -1 : 1), spin * (k % 2 ? -1 : 1) + Math.PI * 1.4); ctx.stroke();
      }
      const rise = alive ? 0.06 + Math.max(0, p.shift ?? 12) / 120 : 0.02;
      for (const q of S.particles) {
        const yy = ((q.y - t * rise * q.sp) % 1 + 1) % 1;
        const [px, py] = at(f, [q.x, yy]);
        glowDot(ctx, px, py, 3 + 4 * q.z, `hsla(${(hue + q.ph * 40) % 360},90%,75%,A)`, (0.25 + 0.5 * env) * q.z * amount * (0.5 + 0.5 * Math.sin(t * 3 + q.ph)));
      }
    },
  };

  function dust(ctx, f, t, speed, colour, size) {
    for (const q of S.particles) {
      const xx = ((q.x + Math.sin(t * 0.13 * q.sp + q.ph) * 0.02) % 1 + 1) % 1;
      const yy = ((q.y - t * 0.012 * q.sp * speed) % 1 + 1) % 1;
      const [px, py] = at(f, [xx, yy]);
      glowDot(ctx, px, py, f.dh * size * q.z, colour, 0.35 * q.z * (0.5 + 0.5 * Math.sin(t * 1.7 + q.ph)));
    }
  }

  function schedule() { if (!S.raf && S.shown) S.raf = requestAnimationFrame(frame); }

  return {
    setScene(key, src) {
      if (key === S.key) return;
      S.key = key;
      const img = new Image();
      img.onload = () => { S.img = img; S.glowKey = ""; };
      img.src = src;
      seedParticles();
    },
    /** The effect's knobs: { mix, decay, pre, time, feedback, pingpong, rate, depth, shift }. */
    setParams(params) { S.params = params; },
    /** From each frame: the song's tempo / position and the mix level (for songs without a tempo). */
    onFrame(f) {
      const now = performance.now();
      if (f.beat) S.beat = { bpm: f.beat.bpm || 0, ppq: f.beat.ppq || 0, bar: f.beat.bar || 0, num: f.beat.num || 4, playing: !!f.beat.playing, at: now };
      const lvl = Math.max(f.peakL ?? -100, f.peakR ?? -100);
      S.level = lvl;
      if (!(S.beat.playing && S.beat.bpm > 0) && lvl > -50) {   // no tempo from the host: follow the hits we hear
        if (lvl > S.avg + 5 && now - S.lastHitAt > 180) S.lastHitAt = now;
        S.avg = 0.9 * S.avg + 0.1 * lvl;
      }
    },
    show(on) { S.shown = on; if (on) schedule(); },
    canvas: g.canvas,
  };
}
