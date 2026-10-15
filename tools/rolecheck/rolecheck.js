// Checks the interface's track-name rules (plugin/ui/roles.js, guessRole) against the table the engine's copy
// (roleFromName, MixAnalysis.cpp) is tested with: both must give every name the same role.
//
// Usage: node tools/rolecheck/rolecheck.js <roles.js> <role_names.txt>

'use strict';
const fs = require('fs');

async function main() {
  const [rolesPath, tablePath] = process.argv.slice(2);
  // roles.js is an ES module with no imports: load its text as one.
  const source = fs.readFileSync(rolesPath, 'utf8');
  const { guessRole } = await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
  let checked = 0, failed = 0;
  for (const line of fs.readFileSync(tablePath, 'utf8').split(/\r?\n/)) {
    const bar = line.indexOf(' | ');
    if (!line || line.startsWith('#') || bar < 0) continue;
    const name = line.slice(0, bar), want = line.slice(bar + 3).trim();
    const got = guessRole(name);
    const gotKey = got === 'other' ? '-' : got;
    ++checked;
    if (gotKey !== want) {
      ++failed;
      console.log(`'${name}': the interface says ${gotKey}, the table ${want}`);
    }
  }
  console.log(`${checked} names checked, ${failed} different.`);
  process.exit(failed === 0 && checked > 50 ? 0 : 1);
}
main().catch((e) => { console.error(e); process.exit(1); });
