// verify_port.mjs -- checks web/engine.js against C++ engine output.
//   node web/verify_port.mjs build/port_vectors.jsonl
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
new Function(readFileSync(join(here, 'engine.js'), 'utf8'))();
const E = globalThis.Engine;

const lines = readFileSync(process.argv[2], 'utf8').trim().split('\n').map(JSON.parse);
const fields = ['call', 'put', 'delta_call', 'delta_put', 'gamma', 'vega',
                'theta_call', 'theta_put', 'rho_call', 'rho_put'];

let bsN = 0, bsMaxAbs = 0, bsMaxPrice = 0;
let treeN = 0, treeMax = 0;
let ivN = 0, threwMatch = 0, threwMismatch = 0;
const ivMax = { engine: 0, naive: 0, cm: 0 };
const iterMismatch = { engine: 0, naive: 0, cm: 0 };
const convMismatch = { engine: 0, naive: 0, cm: 0 };

for (const v of lines) {
  if (v.k === 'bs') {
    const out = E.blackScholes(...v.in);
    fields.forEach((f, i) => {
      const d = Math.abs(out[f] - v.out[i]);
      bsMaxAbs = Math.max(bsMaxAbs, d);
      if (i < 2) bsMaxPrice = Math.max(bsMaxPrice, d);
    });
    bsN++;
  } else if (v.k === 'tree') {
    const [S, K, T, r, s, t, am, steps] = v.in;
    treeMax = Math.max(treeMax, Math.abs(E.binomialTree(S, K, T, r, s, t, !!am, steps).price - v.out));
    treeN++;
  } else if (v.k === 'iv') {
    const [mid, S, K, T, r, t] = v.in;
    if (v.threw) {
      try { E.impliedVol(mid, S, K, T, r, t); threwMismatch++; } catch { threwMatch++; }
      continue;
    }
    ivN++;
    const runs = {
      engine: E.impliedVol(mid, S, K, T, r, t),
      naive: E.impliedVol(mid, S, K, T, r, t, { seed: 0.3 }),
      cm: E.impliedVol(mid, S, K, T, r, t, { seed: E.corradoMiller(mid, S, K, T, r) }),
    };
    for (const k of Object.keys(runs)) {
      const [iv, it, conv] = v[k];
      ivMax[k] = Math.max(ivMax[k], Math.abs(runs[k].iv - iv));
      if (runs[k].iterations !== it) iterMismatch[k]++;
      if ((runs[k].converged ? 1 : 0) !== conv) convMismatch[k]++;
    }
  }
}

const sci = (x) => x.toExponential(2);
console.log(`closed form: ${bsN} inputs x 10 outputs | max |diff| any output ${sci(bsMaxAbs)} | prices ${sci(bsMaxPrice)}`);
console.log(`CRR tree:    ${treeN} inputs | max |price diff| ${sci(treeMax)}`);
console.log(`IV solver:   ${ivN} contracts x 3 seeds | rejected-quote agreement ${threwMatch}/${threwMatch + threwMismatch}`);
for (const k of Object.keys(ivMax))
  console.log(`  ${k.padEnd(6)} max |iv diff| ${sci(ivMax[k])} | iteration-count mismatches ${iterMismatch[k]} | converged mismatches ${convMismatch[k]}`);
