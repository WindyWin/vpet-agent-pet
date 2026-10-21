# 0054. Command-aware activity for shell tools

- Status: Accepted
- Date: 2026-10-20

## Context

Since [0052](0052-tool-aware-animations.md) a tool start carries the class of the
tool's name. Agents do most of their reading and searching through the shell
(`grep`, `rg`, `cat`, `sed -n`, `git diff`), so a Claude `Bash` call, a Codex shell
call or an Antigravity `run_command` always showed `executing`, and `searching` and
`reading` almost never appeared. The hook already examines the command line in
memory for the `risky` bit; it can classify it the same way.

## Decision

When a tool's name maps to `executing` and its command line is non-empty, the
adapter replaces the class with `commandActivity(line)` (`providers/adapters.cpp`):
Claude and Codex `tool_input.command` (string or argument list) and Antigravity's
`toolCall.args.CommandLine`. Only the class leaves the hook, as with `risky`; the
wire field and its closed set of values are unchanged, so `Sessions`, cues and
catalogs need nothing new. OpenCode's plugin sends only the tool name, and making it
send the command would break the privacy rule, so it keeps the tool-name class.

The line is split into simple commands by a helper shared with
`destructiveCommand`: separators (`&&`, `||`, `;`, `|`, `&`, newline, parentheses,
backticks, `$(`), quoting (quoted separators and `>` stay inside their argument, so
`rg "a|b"` is one command; `$( )` inside double quotes still counts), wrappers
(`sudo`, `env`, `xargs`, `timeout 5`, `nice`, ... and their options), `VAR=x`
prefixes, leading shell keywords, and the script of `bash|sh|zsh|dash -c`.
`commandActivity` also drops here-document bodies. Each simple command's program
maps through a small table:

| Class | Programs |
| --- | --- |
| neutral | `cd echo printf export set true false test [ sleep read wait ...` |
| searching | `grep rg ag ack find fd locate ls tree`, `git grep`, `git ls-files` |
| reading | `cat head tail less wc sort cut diff jq stat which ...`; `sed`/`awk`/`perl` without in-place; `git diff log show status blame ...`, `git branch` and `git stash list` |
| browsing | `curl wget http xh gh`, `git fetch`, `git ls-remote` |
| editing | `sed -i`, `perl -i`, `awk -i inplace`, `tee mv cp mkdir touch rm ln chmod patch ...`; `git add commit checkout stash merge rebase ...`; an output redirection to a file (not `/dev/null`, `2>&1`, `/dev/std*`) |
| delegating | `claude codex opencode gemini aider` |
| executing | everything else: builds, tests, interpreters, `./script`, `.`, other `git` subcommands |

The line's class is the most active class among its commands, in the
`activityClasses()` order; an all-neutral line (`cd x`, `echo hi`) stays
`executing`. The tool-name table also gains Codex's `exec_command` and
`local_shell` (executing, so their command is classified) and Claude's `LSP`
(reading).

## Consequences

- `cd starter && grep -n x docs | head; grep -n y t.cpp` now shows searching,
  `git status && git diff` reading, `sed -i ... && cat f` editing, and
  `npm test | tail -5` still executing.
- A wrong class only changes the animation, so the table stays a short heuristic
  rather than a shell parser: unusual quoting, aliases and functions are read by
  their first word.
- `destructiveCommand` reads lines the same way. Its existing cases are unchanged;
  it now also sees through `timeout`, `nice`, wrapper options and leading keywords
  (`if ...; then rm -rf x; fi`), and no longer flags destructive text inside a
  quoted argument (`git commit -m 'fix; rm -rf build'`). Unbalanced quotes fall back
  to the previous quote dropping.
- No command text, program name or argument is sent, logged or retained.

## Validation

### Command classification evidence — 2026-10-20

Linux, Qt 6.11 (offscreen). `cmake --build build -j 8` and
`ctest --test-dir build --output-on-failure -j 4`: 100% of 35 tests passed. The new
`ProviderTests::commandActivities` checks the table rows, priorities, here-documents,
`2>/dev/null` and `2>&1`, `bash -lc` scripts, all-neutral lines, and through the
Claude, Codex and Antigravity adapters that the class is set and `PRIVATE` command
text never appears in the output; non-shell tools keep their class.
`ProviderTests::destructiveCommands` passes unchanged plus the new splitter cases.
`python3 scripts/verify_assets.py` reports every pet valid. Piping Claude
`PreToolUse` `Bash` callbacks (`grep -rn x .`, `cat f`, `cmake --build build`,
`sed -i s/a/b/ f`) through `./build/agent-pet hook --provider claude` exits 0 with
no output.
