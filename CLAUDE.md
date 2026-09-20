@AGENTS.md

<!--
Maintainer note, stripped before it reaches Claude's context.

The project knowledge lives in AGENTS.md so that Codex, Claude Code and any
other AGENTS.md-aware tool read one file with no duplication. This CLAUDE.md
exists only because Claude Code reads CLAUDE.md *in preference to* AGENTS.md
when both are present — so the import on line 1 is what pulls the real content
in. Do not move content back into this file.

Claude Code v2.1.277+ can read AGENTS.md directly, but only when no CLAUDE.md
exists at or above the working directory. The import works on every version and
in sessions that cannot read AGENTS.md at all (Bedrock, telemetry disabled), so
it is the more robust arrangement. A symlink would also work but is a bad idea
on Windows: it needs Administrator or Developer Mode, and git checks a committed
symlink out as a one-line text file unless core.symlinks is enabled.

Note that Codex does NOT expand `@path` imports, so the real content must stay
in AGENTS.md rather than be imported into it.
-->

# Claude Code

The project summary and all shared conventions live in **`AGENTS.md`**, imported
above — read that first. Everything below is Claude Code specific.

## Ghidra over MCP

The bridge in `.mcp.json` needs a **live Ghidra GUI session** with
`MCPServerPlugin` enabled, listening on `localhost:8765`. It is not a headless
tool. If it fails to connect, report the connection failure — do not conclude the
server is unconfigured or that Ghidra access does not exist.

**Never run parallel agents against the MCP bridge.** It acts on Ghidra's single
global `currentProgram`, so concurrent workers will silently write annotations
into the wrong binary and report success. Ghidra has no built-in parallelism
(upstream issue #1990), and the GUI holds a `dreams.lock` that blocks headless
access to the same project.

To parallelise, fan out reads instead:

```
analyzeHeadless <proj> dreams -process WINDREAM.EXE -noanalysis -readOnly \
    -scriptPath ghidra_scripts -postScript Decompile.java 004175bc
```

Reads are parallel-safe; writes are not. Funnel every proposed rename through
`re/symbols/*.tsv` and apply them in one serialised pass with
`ImportSymbols.java`. See `docs/re-setup.md`.

## Delegating to subagents

Findings go to **files, not chat**. A subagent returns a short summary plus a
path; the orchestrator reads the path when merging. Pasting full reports back
through the conversation loses fidelity and burns context.

Give every subagent an objective, an output format, its owned scope, an explicit
list of what it must *not* touch, and an effort budget. Vague delegation makes
workers duplicate each other.

Scratch output belongs in `E:\dreams-work\` — never in the repo, and never in
another project's work directory.

## Codex workers

Codex reads `AGENTS.md` and ignores `CLAUDE.md`, so a `codex exec` run rooted at
the repo root inherits the project summary automatically.

With `--json`, codex writes no startup banner to stderr and `stderr.log` stays
empty — confirm the model and reasoning effort actually took from the session
rollout file instead:

```
~/.codex/sessions/<yyyy>/<mm>/<dd>/rollout-*-<thread_id>.jsonl
```

Grep it for `"model"` and `"reasoning_effort"`. A resume silently falls back to
the user default unless `-m` and the effort flag are repeated.
