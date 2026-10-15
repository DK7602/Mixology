// The DAW words (plugin/ui/daws.js, the same file in all three plug-ins): every phrase has words for every DAW
// (or the "other" words to fall back on), and every {daw:phrase} token in the sources exists.
//   node tools/dawcheck/dawcheck.mjs <daws.js> <source dir or file>...
import { readFileSync, readdirSync, statSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [, , dawsPath, ...sources] = process.argv;
const { DAWS, PHRASES, say, fillDaw, dawName } = await import(pathToFileURL(resolve(dawsPath)).href);
let bad = 0;
const fail = (m) => { console.log("FAIL " + m); bad++; };

for (const p of PHRASES)
  for (const d of DAWS) {
    const w = say(d.key, p);
    if (typeof w !== "string" || !w.length) fail(`${p}: no words for ${d.key}`);
    if (/\{daw/.test(w)) fail(`${p} (${d.key}): a phrase can't hold tokens`);
  }
if (fillDaw("press play in {daw}: {daw:master}", "studioone") !== "press play in Studio One: Main") fail("fillDaw");
if (dawName("other") !== "your DAW" || dawName("nope") !== "your DAW") fail("dawName for an unknown DAW");

const files = [];
const walk = (p) => {
  if (statSync(p).isDirectory()) { for (const f of readdirSync(p)) walk(join(p, f)); return; }
  if (/\.(cpp|h|js|mjs)$/.test(p) && !p.endsWith("daws.js")) files.push(p);
};
sources.forEach(walk);
let tokens = 0;
for (const f of files)
  for (const m of readFileSync(f, "utf8").matchAll(/\{daw:([a-zA-Z]+)\}/g)) {
    tokens++;
    if (m[1] !== "phrase" && !PHRASES.includes(m[1])) fail(`${f}: {daw:${m[1]}} is not in daws.js`);   // "phrase": the docs' placeholder
  }
console.log(`${PHRASES.length} phrases x ${DAWS.length} DAWs, ${tokens} tokens in ${files.length} files`);
process.exit(bad ? 1 : 0);
