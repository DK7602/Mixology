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
// It moves only while the effect is in use on a track (setActive) and the song plays; otherwise it stands still.

import { graph, clamp } from "./ui.js";

// Where things are in each picture (0..1 of the picture's width / height).
const SPOTS = {
  reverb: { centre: [0.49, 0.72] },
  delay: { vanish: [0.795, 0.54], near: { x: 0.245, w: 0.11, top: 0.05, bottom: 0.78 } },
  chorus: { from: [0.24, 0.72], to: [0.68, 0.62] },
  phaser: { column: [0.5, 0.42] },
  hall: { stage: [0.5, 0.97], lights: [[0.505, 0.12], [0.505, 0.4]] },
  room: { kit: [0.31, 0.8], piano: [0.83, 0.78] },
  special: { cube: [0.5, 0.47], radius: 0.5 },   // radius: share of the picture's height
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
    key: null, img: null, glow: null, glowKey: "", params: {}, on: false, active: 0,
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
  const live = () => S.active > 0 && (S.beat.playing || S.level > -60);

  // ---- drawing ----------------------------------------------------------------------------------
  function frame(now) {
    S.raf = 0;
    if (!S.shown) return;
    schedule();
    const still = S.active <= 0;
    if (now - (S.lastDraw || 0) < 1000 / (still ? 4 : 40)) return;   // 40 pictures a second is plenty, and light inside Cubase
    S.lastDraw = now;
    if (!S.img || !S.img.complete || !S.img.naturalWidth) return;
    if (S.glowKey !== S.key + ":" + g.canvas.width) buildGlow();
    const ctx = g.ctx;
    const f = fit();
    if (still) {   // not in use: the picture, standing still
      ctx.save();
      ctx.clearRect(0, 0, w, h);
      ctx.translate(w / 2, h / 2);
      ctx.scale(1.012, 1.012);
      ctx.translate(-w / 2, -h / 2);
      ctx.drawImage(S.img, f.ox, f.oy, f.dw, f.dh);
      ctx.globalCompositeOperation = "lighter";
      ctx.globalAlpha = 0.1;
      ctx.drawImage(S.glow, 0, 0, w, h);
      ctx.restore();
      vignette(ctx);
      return;
    }
    const t = (now - S.t0) / 1000;
    const p = S.params;
    const alive = live();
    const amount = alive ? clamp(0.35 + 0.8 * S.active, 0.45, 1.5) : 0.25;   // stronger as the effect gets louder
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

    vignette(ctx);
  }

  // Vignette, so the motion stays inside the frame.
  function vignette(ctx) {
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
      // Every beat sends a ring of light into the tunnel, then its echoes follow at the delay Time, each a
      // little fainter (Feedback). They all travel the whole way to the far end, fading as they go.
      const [vx, vy] = at(f, SPOTS.delay.vanish), near = SPOTS.delay.near;
      const times = [0.125, 0.25, 1 / 3, 0.5, 0.75, 2 / 3, 1, 1.5, 2];   // the Time knob's note values, in beats
      const beat = beatSeconds();
      const step = times[clamp(Math.round(p.time ?? 3), 0, 8)] * beat;
      const fb = clamp((p.feedback ?? 35) / 100, 0, 0.95), pingPong = (p.pingpong ?? 60) / 100;
      const travel = clamp(beat * 3, 1.2, 4);   // seconds from the front to the far end
      const nx = f.ox + near.x * f.dw, nTop = f.oy + near.top * f.dh, nBottom = f.oy + near.bottom * f.dh, nw = near.w * f.dw;
      ctx.save();
      ctx.shadowColor = "rgba(255,190,80,1)";
      for (let k = 0; k < 4; k++) {               // this beat and the three before (their echoes are still on the way)
        const beatAge = b.age + k * beat;
        for (let i = 0; i < 12; i++) {
          const age = beatAge - i * step;
          if (age < 0) break;
          const d = age / travel;
          if (d >= 1) continue;
          const level = i === 0 ? 1 : Math.max(0.22, Math.pow(fb, i));   // faint echoes stay visible to the end
          const a = amount * level * Math.pow(1 - d, 0.7) * (k === 0 ? 1 : 0.85);
          if (a < 0.015) continue;
          const sc = 1 / (1 + 9 * d);              // perspective: far away is small and close together
          const swing = (i % 2 ? 1 : -1) * pingPong * 0.05 * f.dw * sc * (i ? 1 : 0);
          const x = vx + (nx - vx) * sc + swing, top = vy + (nTop - vy) * sc, bottom = vy + (nBottom - vy) * sc, aw = nw * sc;
          ctx.shadowBlur = 6 + 16 * sc;
          ctx.strokeStyle = `rgba(255,${200 + Math.round(30 * d)},${110 + Math.round(60 * d)},${clamp(a, 0, 1)})`;
          ctx.lineWidth = 1 + 3.5 * sc;
          ctx.beginPath();
          ctx.moveTo(x - aw / 2, bottom);
          ctx.lineTo(x - aw / 2, top + aw / 2);
          ctx.arc(x, top + aw / 2, aw / 2, Math.PI, 0);
          ctx.lineTo(x + aw / 2, bottom);
          ctx.stroke();
        }
      }
      ctx.restore();
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
      if (alive) {
        // A wave of light rolls from the stage out to the edges of the hall on every beat; a long Decay lets the
        // last beat's wave linger behind the new one.
        const beat = beatSeconds();
        const far = Math.max(Math.hypot(sx, sy), Math.hypot(w - sx, sy)) + 40;
        const decay = clamp((p.decay ?? 3.2) * 0.4, 0.5, 5);
        for (let k = 1; k >= 0; k--) {
          const age = b.age + k * beat;
          const progress = clamp(age / (beat * 0.95), 0, 1.4);
          if (progress >= 1.4) continue;
          const r = 30 + (far - 30) * Math.pow(Math.min(progress, 1), 0.85) + (progress > 1 ? (progress - 1) * 200 : 0);
          const a = 0.45 * amount * Math.exp(-age / decay) * (progress > 1 ? Math.max(0, 1 - (progress - 1) / 0.4) : 1) * (k ? 0.6 : 1);
          if (a < 0.01) continue;
          const band = 60 + 90 * Math.min(progress, 1);
          // The hall's own lights flare up where the wave is passing ...
          ctx.save();
          ctx.beginPath();
          ctx.arc(sx, sy, r, 0, Math.PI * 2);
          ctx.arc(sx, sy, Math.max(0, r - band), 0, Math.PI * 2, true);
          ctx.clip();
          ctx.globalAlpha = clamp(a * 2.2, 0, 1);
          ctx.drawImage(S.glow, 0, 0, w, h);
          ctx.drawImage(S.glow, 0, 0, w, h);
          ctx.restore();
          // ... behind a bright front.
          const gr = ctx.createRadialGradient(sx, sy, Math.max(0, r - band), sx, sy, r);
          gr.addColorStop(0, "rgba(255,200,120,0)"); gr.addColorStop(0.8, `rgba(255,225,170,${a * 0.6})`); gr.addColorStop(0.97, `rgba(255,245,220,${a})`); gr.addColorStop(1, "rgba(255,200,120,0)");
          ctx.fillStyle = gr; ctx.fillRect(0, 0, w, h);
        }
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
      const beat = beatSeconds();
      const hue = (t * 40 + b.index * 47) % 360;
      const motion = (p.motion ?? 35) / 100;

      // The crystal turns a step on every beat (snaps on the beat, then settles), more with Motion.
      const stepDeg = 20 + 40 * motion;
      const frac = alive ? clamp(b.age / Math.min(0.35, beat * 0.6), 0, 1) : 0;
      const eased = 1 - Math.pow(1 - frac, 3);
      const angle = alive ? ((b.index + eased) * stepDeg) * Math.PI / 180 : Math.sin(t * 0.3) * 0.08;
      spinPatch(ctx, f, cx, cy, SPOTS.special.radius * f.dh, angle);

      // Light beams turning behind it.
      ctx.save();
      ctx.translate(cx, cy);
      ctx.rotate(t * (0.15 + 0.5 * motion) * (alive ? 1 : 0.3));
      for (let k = 0; k < 8; k++) {
        ctx.rotate(Math.PI / 4);
        const len = f.dh * (0.9 + 0.3 * env);
        const gr = ctx.createLinearGradient(0, 0, len, 0);
        gr.addColorStop(0, `hsla(${(hue + k * 45) % 360},95%,70%,${clamp((alive ? 0.12 + 0.3 * env : 0.05) * amount, 0, 1)})`);
        gr.addColorStop(1, "hsla(0,0%,100%,0)");
        ctx.fillStyle = gr;
        ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(len, -len * 0.05); ctx.lineTo(len, len * 0.05); ctx.closePath(); ctx.fill();
      }
      ctx.restore();

      glowDot(ctx, cx, cy, f.dh * 0.42, `hsla(${hue},90%,62%,A)`, (alive ? 0.2 + 0.55 * env : 0.12) * amount);

      if (alive) {
        // Shock waves from the crystal on every beat (more of them with Feedback), a white flash on the bar's first beat.
        const rings = 1 + Math.round(((p.feedback ?? 55) / 100) * 3);
        for (let k = 0; k < rings; k++) {
          const age = b.age - k * 0.09;
          if (age < 0) continue;
          const rr = f.dh * (0.12 + age * 1.6), a = 0.6 * amount * Math.exp(-age / 0.45);
          if (a < 0.01) continue;
          ctx.strokeStyle = `hsla(${(hue + k * 70) % 360},95%,70%,${clamp(a, 0, 1)})`;
          ctx.lineWidth = 3;
          ctx.beginPath(); ctx.ellipse(cx, cy, rr * 1.6, rr * 0.55, -0.12, 0, Math.PI * 2); ctx.stroke();
        }
        if (b.inBar === 0) glowDot(ctx, cx, cy, f.dh * 0.8, "rgba(255,255,255,A)", 0.35 * Math.exp(-b.age / 0.15) * amount);

        // Sparks burst out of the crystal on every beat.
        const r = rng(b.index * 7919 + 17);
        for (let i = 0; i < 26; i++) {
          const dir = r() * Math.PI * 2, speed = (0.5 + r()) * f.dh * 1.4, life = 0.35 + r() * 0.5;
          if (b.age > life) continue;
          const dist = speed * b.age * (1 - b.age / (2 * life));
          const px = cx + Math.cos(dir) * dist * 1.5, py = cy + Math.sin(dir) * dist * 0.8;
          glowDot(ctx, px, py, 3 + 4 * r(), `hsla(${(hue + i * 13) % 360},95%,75%,A)`, 0.9 * amount * (1 - b.age / life));
        }
      }

      // Orbits, and particles rising (falling with a negative Shift).
      const spin = t * (alive ? 0.9 : 0.25);
      for (let k = 0; k < 3; k++) {
        ctx.strokeStyle = `hsla(${(hue + k * 60) % 360},90%,70%,${clamp((0.25 + 0.4 * env) * amount, 0, 1)})`;
        ctx.lineWidth = 2;
        ctx.beginPath(); ctx.ellipse(cx, cy, f.dh * (0.36 + k * 0.06), f.dh * (0.07 + k * 0.02), 0.25 - k * 0.12, spin * (k % 2 ? -1 : 1), spin * (k % 2 ? -1 : 1) + Math.PI * 1.4); ctx.stroke();
      }
      const shift = p.shift ?? 12;
      const rise = alive ? (0.04 + Math.abs(shift) / 120) * (shift < 0 ? -1 : 1) : 0.02;
      for (const q of S.particles) {
        const yy = ((q.y - t * rise * q.sp) % 1 + 1) % 1;
        const [px, py] = at(f, [q.x, yy]);
        glowDot(ctx, px, py, 3 + 4 * q.z, `hsla(${(hue + q.ph * 40) % 360},90%,75%,A)`, (0.25 + 0.5 * env) * q.z * amount * (0.5 + 0.5 * Math.sin(t * 3 + q.ph)));
      }
    },
  };

  /** Turns a round part of the picture (the crystal) about its centre, with a soft edge. */
  function spinPatch(ctx, f, cx, cy, radius, angle) {
    const k = g.canvas.width / w;
    const size = Math.ceil(radius * 2 * k);
    if (!S.patch || S.patch.width !== size) { S.patch = document.createElement("canvas"); S.patch.width = S.patch.height = size; }
    const pc = S.patch.getContext("2d");
    pc.setTransform(1, 0, 0, 1, 0, 0);
    pc.globalCompositeOperation = "source-over";
    pc.clearRect(0, 0, size, size);
    pc.setTransform(k, 0, 0, k, 0, 0);
    pc.translate(radius, radius);
    pc.rotate(angle);
    pc.translate(-cx, -cy);
    pc.drawImage(S.img, f.ox, f.oy, f.dw, f.dh);
    pc.setTransform(1, 0, 0, 1, 0, 0);
    pc.globalCompositeOperation = "destination-in";   // soft round edge
    const m = pc.createRadialGradient(size / 2, size / 2, size * 0.36, size / 2, size / 2, size / 2);
    m.addColorStop(0, "rgba(0,0,0,1)");
    m.addColorStop(1, "rgba(0,0,0,0)");
    pc.fillStyle = m;
    pc.fillRect(0, 0, size, size);
    const prev = ctx.globalCompositeOperation;
    ctx.globalCompositeOperation = "source-over";
    ctx.drawImage(S.patch, cx - radius, cy - radius, radius * 2, radius * 2);
    ctx.globalCompositeOperation = prev;
  }

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
      img.onload = () => { if (S.key === key) { S.img = img; S.glowKey = ""; } };   // (another effect picked meanwhile: not this picture)
      img.src = src;
      seedParticles();
    },
    /** The effect's knobs: { mix, decay, pre, time, feedback, pingpong, rate, depth, shift }. */
    setParams(params) { S.params = params; },
    /** 0: the effect isn't used (the picture stands still); up to 1.2: in use, livelier as it gets louder. */
    setActive(level) { S.active = Math.max(0, +level || 0); },
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
