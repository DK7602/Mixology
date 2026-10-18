// The Space page's copy of the Space layers rule (procspec.js spaceLayers, generated from the engine's table) gives
// what the engine's proc::spaceLayers() gives (tests/test_space.cpp checks the same cases there).
//   node spacecheck.mjs <path to procspec.js>
import { pathToFileURL } from "node:url";

const spec = await import(pathToFileURL(process.argv[2]).href);
const { PROC, PROC_INDEX, SPACE_ON, spaceLayers } = spec;
const REVERB = 0, DELAY = 1, CHORUS = 2, PHASER = 3, HALL = 4, ROOM = 5, SPECIAL = 6;

function values(on, extra = {}) {
  const v = PROC.map((p) => p.def);
  v[PROC_INDEX.sp_on] = 1;
  for (const t of on) v[PROC_INDEX[SPACE_ON[t]]] = 1;
  for (const [id, x] of Object.entries(extra)) v[PROC_INDEX[id]] = x;
  return (id) => v[PROC_INDEX[id]];
}

const cases = [
  ["a project from before: Space Effect alone", values([], { sp_type: HALL }), [HALL]],
  ["the user's Phaser + Hall, in the order they run", values([HALL, PHASER]), [PHASER, HALL]],
  ["all seven switched on: the first three in the chain's order", values([REVERB, DELAY, CHORUS, PHASER, HALL, ROOM, SPECIAL]), [CHORUS, PHASER, DELAY]],
  ["four spaces: the first three", values([SPECIAL, ROOM, HALL, REVERB]), [REVERB, HALL, ROOM]],
  ["a switch on wins over Space Effect", values([DELAY], { sp_type: HALL }), [DELAY]],
  ["a damaged Space Effect: the first effect", values([], { sp_type: NaN }), [REVERB]],
  ["out of range: the last effect", values([], { sp_type: 9 }), [SPECIAL]],
];
let failed = 0;
for (const [name, get, want] of cases) {
  const got = spaceLayers(get);
  const ok = JSON.stringify(got) === JSON.stringify(want);
  if (!ok) failed++;
  console.log(`${ok ? "ok  " : "FAIL"} ${name}: ${JSON.stringify(got)}${ok ? "" : " (want " + JSON.stringify(want) + ")"}`);
}
if (SPACE_ON.length !== 7 || SPACE_ON.some((id) => PROC_INDEX[id] === undefined)) { console.log("FAIL the on switches"); failed++; }
process.exit(failed ? 1 : 0);
