// Match statistics: Elo estimates and the sequential probability ratio test.

// Elo difference implied by A's score, with a 95% confidence interval.
export function eloEstimate({ win, draw, loss }) {
  const n = win + draw + loss;
  const score = (win + draw / 2) / n;
  const variance = (win * (1 - score) ** 2 + draw * (0.5 - score) ** 2 + loss * score ** 2) / n;
  const margin = 1.96 * Math.sqrt(variance / n);
  return { elo: scoreToElo(score), low: scoreToElo(score - margin), high: scoreToElo(score + margin) };
}

export function scoreToElo(score) {
  if (score <= 0) return -Infinity;
  if (score >= 1) return Infinity;
  return -400 * Math.log10(1 / score - 1) + 0; // + 0 turns -0 into 0
}

const eloToScore = (elo) => 1 / (1 + 10 ** (-elo / 400));

export function parseSprt(text, alpha, beta) {
  const [elo0, elo1] = text.split(',').map(Number);
  if (!Number.isFinite(elo0) || !Number.isFinite(elo1) || elo1 <= elo0) throw new Error('--sprt needs elo0,elo1 with elo1 > elo0');
  return { elo0, elo1, alpha, beta, lower: Math.log(beta / (1 - alpha)), upper: Math.log((1 - beta) / alpha) };
}

// Log-likelihood ratio of H1 over H0 for the results so far, using the
// normal approximation of the generalized SPRT (as in Fishtest).
export function sprtLlr({ win, draw, loss }, { elo0, elo1 }) {
  const n = win + draw + loss;
  if (win === 0 || loss === 0) return 0; // too little information yet
  const score = (win + draw / 2) / n;
  const variance = (win * (1 - score) ** 2 + draw * (0.5 - score) ** 2 + loss * score ** 2) / n;
  const s0 = eloToScore(elo0);
  const s1 = eloToScore(elo1);
  return (n * (s1 - s0) * (2 * score - s0 - s1)) / (2 * variance);
}
