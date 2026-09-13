# CLAUDE.md

This repository is the interface of **ROCKNIX**, an immutable Linux distribution
for handheld gaming devices. It is built as the `emulationstation` package of the
distribution repository, and every rule about how to work in it lives **there**,
not here -- one copy, so nothing drifts (fork rule D-WORKFLOW-010, 2026-09-13).

**Read these first**, from the distribution checkout at
`/workspace/repos/rocknix` (branch `next`; a feature worktree cut from an older
base may lack them, so read from `next`):

| File | What it decides |
| --- | --- |
| `.claude/rules/es-native-ui.md` | where the code lives, the page / card / background-job patterns, spacing, the four surface tiers |
| `.claude/rules/es-player-text.md` | every word a player reads: the tiers and verbs, naming, how much a row may carry |
| `.claude/rules/es-code-traps.md` | sharp edges of this codebase, each with its fix (fonts scale twice on small panels, `exists()` caches, the help bar under full-screen menus, ...) |
| `.claude/rules/es-ui-style-guide.md` | how a screen looks and behaves: row builders, confirmations, waiting, saving, reboot flags |
| `.claude/rules/player-language.md`, `least-surprise.md`, `time-to-play.md`, `vm-first.md` | the principles behind them |
| `docs/decision-register.md` | settled decisions, cited by ID (`D-UI-050` and the like); append-only |

**Where things happen.** Work is done from a rocknix worktree by absolute path
(so those rules are in context), on a `feature/<name>` branch here, merged into
**`test/qa-integration`** -- the branch the distribution's package pins by full
commit (`projects/ROCKNIX/packages/ui/emulationstation/package.mk`,
`PKG_VERSION`). A change is in an image only after that pin moves. Every UI change
is framed on the GENERIC_X64 VM at **640x480** as well as the pair's 1280x800
before it is called done (blindspot 41): fonts scale by 1.31 under 720 px and
full-screen menus are on there.

**Checks that read this tree**, all run from the distribution checkout:
`python3 tests/cloud-oauth-lifetime.py` (page lifetimes under AddressSanitizer),
`tools/vocabulary-check` (player words), `ES_SRC=<this checkout>
tools/register-check` (register citations). Comments before a translatable
`_("")` string must be ASCII or the image build fails.

**Upstream.** `upstream` is `ROCKNIX/emulationstation-next`. A PR branch is
`pr/<name>`, built by content from `test/qa-integration` and containing only
source; this file, `.githooks/` and `.claude/` never travel -- `.githooks/pre-push`
refuses a `pr/*` push that carries them. Enable it once per clone:

```bash
git config core.hooksPath "$(git rev-parse --show-toplevel)/.githooks"
```
