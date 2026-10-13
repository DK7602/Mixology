// Track roles: icon + colour (the mockup's palette). Mixology guesses them from the channel name, and
// Analyze Mix from the sound too; you can correct any of them (click the icon on the Tracks page).

export const ROLES = {
  kick:   { icon: "kick",   colour: "#ff2b2b", label: "Kick" },
  snare:  { icon: "snare",  colour: "#3dff8a", label: "Snare" },
  hats:   { icon: "hats",   colour: "#ff3fa4", label: "Hats" },
  perc:   { icon: "perc",   colour: "#26d4ff", label: "Perc" },
  bass:   { icon: "bass",   colour: "#ffd21f", label: "Bass" },
  keys:   { icon: "keys",   colour: "#28c8ff", label: "Keys" },
  synth:  { icon: "synth",  colour: "#2fb6ff", label: "Synth" },
  guitar: { icon: "guitar", colour: "#b04dff", label: "Guitar" },
  vox:    { icon: "vox",    colour: "#3dff6e", label: "Vocals" },
  fx:     { icon: "fx",     colour: "#ffe23a", label: "FX" },
  adlibs: { icon: "adlibs", colour: "#ff9a1f", label: "Ad-libs" },
  other:  { icon: "other",  colour: "#c0c8d6", label: "Other" },
};

export function guessRole(name) {
  const n = (name || "").toLowerCase();
  const rules = [
    ["adlibs", /ad.?lib|adlib|hype|dub vox|backing|bgv|double/],
    ["vox", /vox|vocal|voc\b|lead v|verse|hook|rap|sing|acap/],
    ["kick", /kick|kik|\bbd\b|bassdrum/],
    ["snare", /snare|snr|clap|\bsd\b|rim/],
    ["hats", /hat|\bhh\b|cymbal|ride|crash|open h/],
    ["perc", /perc|shaker|tamb|conga|bongo|tom|cowbell|drum/],
    ["bass", /808|bass|sub\b|subs/],
    ["keys", /key|piano|rhodes|organ|\bep\b|wurl/],
    ["guitar", /guitar|gtr|acoustic|strat|electric/],
    ["synth", /synth|pad|lead|pluck|arp|string|brass|bell|choir|sample|melod|loop/],
    ["fx", /\bfx\b|riser|sweep|impact|reverse|noise|verb|delay|transition/],
  ];
  for (const [role, re] of rules) if (re.test(n)) return role;
  return "other";
}


export const ROLE_ORDER = ["kick", "snare", "hats", "perc", "bass", "keys", "synth", "guitar", "vox", "adlibs", "fx", "other"];

// The analyzer's six colours, and which roles belong to each (same as the engine's groups).
export const GROUPS = [
  { key: "kick", label: "Kick", colour: "#ff2424", roles: ["kick"] },
  { key: "bass", label: "Bass", colour: "#ff7a12", roles: ["bass"] },
  { key: "drums", label: "Drums", colour: "#ffe11f", roles: ["snare", "hats", "perc"] },
  { key: "vocals", label: "Vocals", colour: "#3dff5a", roles: ["vox", "adlibs"] },
  { key: "instruments", label: "Instruments", colour: "#1f6bff", roles: ["keys", "synth", "guitar"] },
  { key: "other", label: "Other", colour: "#e62bff", roles: ["fx", "other"] },
];
