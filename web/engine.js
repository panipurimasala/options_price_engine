// engine.js -- line-for-line JavaScript port of the C++ engine in functions/.
//
//   blackScholes   <- functions/black_scholes.cpp
//   impliedVol     <- functions/iv_solver.cpp   (bracketed Newton-Raphson)
//   binomialTree   <- functions/binomial_tree.cpp (Cox-Ross-Rubinstein)
//
// The C++ engine stays the source of truth. web/verify_port.cpp prints the
// C++ results on a grid of inputs and on every validation contract;
// web/verify_port.mjs checks this file against them. Re-run both after any
// change to the C++.
//
// One difference: C++ computes N(x) with std::erfc, which JavaScript lacks.
// normalCDF below uses Hart's double-precision algorithm (as given in
// G. West, "Better approximations to cumulative normal functions", 2005),
// accurate to roughly 1e-14 -- the verifier measures the effect on prices.
(function (root) {
  'use strict';

  const PI = Math.PI;

  function normalPDF(x) {
    return Math.exp(-0.5 * x * x) / Math.sqrt(2.0 * PI);
  }

  function normalCDF(x) {
    const a = Math.abs(x);
    let c;
    if (a > 37) {
      c = 0;
    } else {
      const e = Math.exp(-a * a / 2);
      if (a < 7.07106781186547) {
        let n = 3.52624965998911e-2 * a + 0.700383064443688;
        n = n * a + 6.37396220353165;
        n = n * a + 33.912866078383;
        n = n * a + 112.079291497871;
        n = n * a + 221.213596169931;
        n = n * a + 220.206867912376;
        let d = 8.83883476483184e-2 * a + 1.75566716318264;
        d = d * a + 16.064177579207;
        d = d * a + 86.7807322029461;
        d = d * a + 296.564248779674;
        d = d * a + 637.333633378831;
        d = d * a + 793.826512519948;
        d = d * a + 440.413735824752;
        c = e * n / d;
      } else {
        let b = a + 0.65;
        b = a + 4 / b;
        b = a + 3 / b;
        b = a + 2 / b;
        b = a + 1 / b;
        c = e / b / 2.506628274631;
      }
    }
    return x > 0 ? 1 - c : c;
  }

  function blackScholes(S, K, T, r, sigma) {
    if (!(S > 0)) throw new RangeError('S must be > 0');
    if (!(K > 0)) throw new RangeError('K must be > 0');
    if (!(T > 0)) throw new RangeError('T must be > 0');
    if (!(sigma > 0)) throw new RangeError('sigma must be > 0');

    const sqrtT = Math.sqrt(T);
    const discount = Math.exp(-r * T);
    const d1 = (Math.log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * sqrtT);
    const d2 = d1 - sigma * sqrtT;

    const Nd1 = normalCDF(d1);
    const Nd2 = normalCDF(d2);
    const Nd1n = 1.0 - Nd1;
    const Nd2n = 1.0 - Nd2;
    const phi = normalPDF(d1);

    return {
      call: S * Nd1 - K * discount * Nd2,
      put: K * discount * Nd2n - S * Nd1n,
      delta_call: Nd1,
      delta_put: Nd1 - 1.0,
      gamma: phi / (S * sigma * sqrtT),
      vega: S * phi * sqrtT / 100.0,                      // per 1 vol point
      theta_call: (-(S * phi * sigma) / (2.0 * sqrtT) - r * K * discount * Nd2) / 365.0,
      theta_put: (-(S * phi * sigma) / (2.0 * sqrtT) + r * K * discount * Nd2n) / 365.0,
      rho_call: K * T * discount * Nd2 / 100.0,           // per 1% rate move
      rho_put: -K * T * discount * Nd2n / 100.0,
    };
  }

  function corradoMiller(marketPrice, S, K, T, r) {
    const d = Math.exp(-r * T);
    const Kd = K * d;
    const denominator = S + Kd;
    if (denominator <= 0.0 || T <= 0.0) return 0.20;
    const intrinsic = (S - Kd) / 2.0;
    const pmi = marketPrice - intrinsic;
    let inner = pmi * pmi - ((S - Kd) * (S - Kd)) / PI;
    if (inner < 0.0) inner = 0.0;
    const sigma = (Math.sqrt(2.0 * PI / T) / denominator) * (pmi + Math.sqrt(inner));
    return Math.max(0.005, Math.min(sigma, 5.0));
  }

  function brennerSubrahmanyam(marketPrice, S, T) {
    return (marketPrice / S) * Math.sqrt(2.0 * PI / T);
  }

  // type: 'C' or 'P'. opts.seed overrides the starting guess -- used only by
  // the iteration study on the page; the engine itself has no such option.
  function impliedVol(marketPrice, S, K, T, r, type, opts) {
    const tol = 1e-6, maxIter = 100;
    if (!(S > 0) || !(K > 0)) throw new RangeError('S and K must be > 0');
    if (!(T > 0)) throw new RangeError('T must be > 0');
    if (!(marketPrice > 0)) throw new RangeError('market price must be > 0');

    const discount = Math.exp(-r * T);
    const eps = 1e-6;
    if (type === 'C') {
      const lower = Math.max(S - K * discount, 0.0);
      if (marketPrice <= lower - eps || marketPrice >= S + eps)
        throw new RangeError('Call price outside no-arbitrage bounds');
    } else {
      const lower = Math.max(K * discount - S, 0.0);
      const upper = K * discount;
      if (marketPrice <= lower - eps || marketPrice >= upper + eps)
        throw new RangeError('Put price outside no-arbitrage bounds');
    }

    let sigma;
    if (opts && typeof opts.seed === 'number') {
      sigma = opts.seed;
    } else {
      sigma = brennerSubrahmanyam(marketPrice, S, T);
      if (sigma < 0.005 || sigma > 5.0) sigma = corradoMiller(marketPrice, S, K, T, r);
    }

    const path = opts && opts.trace ? [] : null;
    let lo = 0.0, hi = 10.0;
    for (let i = 0; i < maxIter; ++i) {
      const res = blackScholes(S, K, T, r, sigma);
      const price = type === 'C' ? res.call : res.put;
      const vega = res.vega * 100.0;
      const error = price - marketPrice;
      if (path) path.push({ sigma, error, lo, hi });
      if (Math.abs(error) < tol) return { iv: sigma, iterations: i + 1, converged: true, path };
      if (error < 0.0) lo = sigma; else hi = sigma;
      let next = Math.abs(vega) > 1e-10 ? sigma - error / vega : lo - 1.0;
      let step = 'newton';
      if (!(next > lo && next < hi)) { next = 0.5 * (lo + hi); step = 'bisect'; }
      if (path) path[path.length - 1].step = step;
      sigma = next;
    }
    return { iv: sigma, iterations: maxIter, converged: false, path };
  }

  // type: 'C' or 'P'; american: boolean.
  function binomialTree(S, K, T, r, sigma, type, american, steps) {
    if (!(S > 0) || !(K > 0)) throw new RangeError('S and K must be > 0');
    if (!(T > 0)) throw new RangeError('T must be > 0');
    if (!(sigma > 0)) throw new RangeError('sigma must be > 0');
    if (!(steps > 0)) throw new RangeError('steps must be > 0');

    const N = steps;
    const dt = T / steps;
    const u = Math.exp(sigma * Math.sqrt(dt));
    const d = 1.0 / u;
    const discount = Math.exp(-r * dt);
    const p = (Math.exp(r * dt) - d) / (u - d);
    const q = 1.0 - p;
    if (p <= 0.0 || p >= 1.0)
      throw new RangeError('Risk-neutral probability outside (0,1): reduce steps or check inputs');

    const V = new Float64Array(N + 1);
    for (let j = 0; j <= N; ++j) {
      const spot = S * Math.pow(u, j) * Math.pow(d, N - j);
      V[j] = type === 'C' ? Math.max(spot - K, 0.0) : Math.max(K - spot, 0.0);
    }
    for (let i = N; i-- > 0;) {
      for (let j = 0; j <= i; ++j) {
        const cont = discount * (p * V[j + 1] + q * V[j]);
        if (american) {
          const spot = S * Math.pow(u, j) * Math.pow(d, i - j);
          const ex = type === 'C' ? Math.max(spot - K, 0.0) : Math.max(K - spot, 0.0);
          V[j] = Math.max(cont, ex);
        } else {
          V[j] = cont;
        }
      }
    }
    return { price: V[0], steps };
  }

  root.Engine = { blackScholes, impliedVol, binomialTree, corradoMiller, brennerSubrahmanyam, normalCDF };
})(typeof window !== 'undefined' ? window : globalThis);
