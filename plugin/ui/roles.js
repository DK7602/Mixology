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

/** The icon for a track: its role's, except a percussion track that is a whole kit ("Drums", "Drum Machine", "Kit")
    shows a drum kit. */
export function roleIcon(role, name) {
  if (role === "perc" && /drum|\bkit\b|machine|\bdrm/i.test(name || "")) return "drums";
  return (ROLES[role] || ROLES.other).icon;
}

/** A name as words: letters and digits only, lower case, one space between words; a new word also starts where a
    lower-case letter meets a capital ("LeadVox") and where letters meet digits ("Vox2", "808s"). */
export function nameWords(name) {
  let out = "", prev = "";
  for (const c of name || "") {
    if (!/[A-Za-z0-9]/.test(c)) {
      if (out && !out.endsWith(" ")) out += " ";
      prev = "";
      continue;
    }
    const split = prev && ((/[a-z]/.test(prev) && /[A-Z]/.test(c)) || (/[0-9]/.test(prev) !== /[0-9]/.test(c)));
    if (split && !out.endsWith(" ")) out += " ";
    out += c.toLowerCase();
    prev = c;
  }
  return out.trimEnd();
}

// Whole words, the same rules as roleFromName() in the engine (MixAnalysis.cpp), both checked against
// tests/role_names.txt. What the track IS wins over the part of the song it plays ("Keys - Verse" = keys);
// a song part alone ("Verse 1", "Hook") is taken as a vocal.
const W = (words) => new RegExp("\\b(" + words + ")\\b");
const NAME_RULES = {
  fxReturn: W("reverbs?|verbs?|delays?|echo(es)?|plate|hall|fx|sfx|efx|returns?"),
  adlib: W("ad ?libs?|adlibs?|hypes?|bgvs?|bvs?"),
  vocal: W("vox|vocals?|voc|vx|lv|voices?|raps?|rapper|rapping|sing|singer|singing|acapella|acappella|a ?cappella|acap|talkbox|vocoder|speech|spoken"),
  vocalPart: W("backing|background|backups?|doubles?|dbls?|dubs?|harmony|harmonies|harms?|stacks?|layers?|chants?|whispers?|oohs?|ahhs?"),
  instruments: [
    ["kick", W("kicks?|kik|bd|bass ?drums?|kickdrums?")],
    ["snare", W("snares?|snr|claps?|sd|rims?|rimshots?|snaps?")],
    ["hats", W("hats?|hi ?hats?|hihats?|hh|cymbals?|rides?|crash(es)?")],
    ["perc", W("percs?|percussion|shakers?|tamb|tambourines?|tambo|congas?|bongos?|toms?|cowbells?|triangle|claves?|woodblocks?|timbales?|drums?|drm|kits?|drumkits?|breakbeats?|tops?")],
    ["bass", W("808s?|bass(es)?|basslines?|subs?|reese")],
    ["keys", W("keys?|keyboards?|pianos?|rhodes|organs?|ep|epiano|wurli|wurlitzer|clavs?|clavinet|harpsichord")],
    ["guitar", W("guitars?|gtrs?|acoustic|strat|tele|electric|ukes?|ukulele|banjo|mandolin")],
    ["synth", W("synths?|pads?|leads?|plucks?|arps?|arpeggios?|strings?|brass|horns?|trumpets?|sax|flutes?|bells?|choirs?|samples?|melody|melodies|melodic|chords?|stabs?|loops?")],
  ],
  fxSound: W("effects?|transitions?|risers?|sweeps?|impacts?|reverse|noise|uplifters?|downlifters?|whoosh(es)?|swells?|sirens?|scratch(es)?|vinyl|foley|atmos|atmosphere|ambience|textures?"),
  songPart: W("verses?|hooks?|refrains?|prehooks?|pre hooks?"),
};

export function guessRole(name) {
  const w = nameWords(name), R = NAME_RULES;
  if (!w) return "other";
  if (R.fxReturn.test(w)) return "fx";
  if (R.adlib.test(w)) return "adlibs";
  if (R.vocal.test(w)) return R.vocalPart.test(w) ? "adlibs" : "vox";
  for (const [role, re] of R.instruments) if (re.test(w)) return role;
  if (R.vocalPart.test(w)) return "adlibs";
  if (R.fxSound.test(w)) return "fx";
  if (R.songPart.test(w)) return "vox";
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
