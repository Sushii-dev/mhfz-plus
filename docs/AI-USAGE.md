# AI usage

This project was built with AI. We say so up front so that everyone can decide for themselves how to treat it.

## Who did what

**The AI (Anthropic's Claude, through the Claude Code agent)** wrote nearly all of the code and documentation:

- the reverse engineering of MHF-Z's renderer and frame limiter (static analysis with Ghidra and Python scripts),
- the frame-rate mod (`src/`), the launcher, the installer scripts and the settings app,
- the test app, the offline benches and the analysis tools,
- this documentation.

**The maintainer, a person:**

- set the goals and the rules: no added input latency, never slow the game's tick, never patch game code, stay
  low-profile;
- played every candidate build in the real game for many hours and described what looked wrong;
- recorded clips of each glitch with the built-in recorder (most fixes started from one of those clips);
- decided what shipped.

**The English translation** (the optional part of the launcher) was machine-translated with Gemma, Google's open
model, and then went through automated review passes. No human translator checked it line by line, so expect odd
lines.

## What this means for you

- **Review it like any other code.** The mod has a large test setup (see [TESTING.md](TESTING.md)) and many hours of
  real play, but few human experts have read the code. Read it before you trust it, and please report problems.
- **The design notes say where things were measured and where they're guesses.** [ENGINE-NOTES.md](ENGINE-NOTES.md)
  marks unconfirmed points as believed or unknown.
- **Copyright.** In some countries, purely AI-generated material may not be protected by copyright. The license
  grants whatever rights the maintainer has; it doesn't claim more.
- **No copied code.** Nothing from Capcom, and no code from other projects was pasted in. Anything this builds on is
  listed in [CREDITS.md](CREDITS.md).

## Contributions

Contributions written with AI help are welcome, the same as any others. Say in the pull request that AI was used and
how you tested the change. In-game testing matters more than how the code was written.
