# Chess engine

Two engines in one repo, built and run separately.

- [`engine-1/`](engine-1/): JavaScript engine, versions 1.0 to 1.6, with the
  playable board app. Frozen at 1.6 and kept as the baseline. See
  `engine-1/README.md` and `engine-1/HANDOFF.md`.
- [`engine-2/`](engine-2/): C++ engine, the main line from here on.

Run Engine 1 from inside its folder: `cd engine-1 && npm test`. To pick up
the work in a new session, start with `engine-2/HANDOFF.md`.
