# Ollama logs and privacy

All eleven applications append full communication to `<game>-ollama.log` and failures to
`<game>-ollama-error.log` in the working directory. They do not rotate or delete these
files automatically. Here `<game>` is `tic-tac-toe`, `four-in-a-row`, `pentalpha`, `heptalpha`, `peg-solitaire`, `pentalpha-crossed-circle`, `reversi`, `hex`, `tavli`, `sokoban`, or `go`.
Long sessions and reasoning models can generate large logs. The error log can also contain request/response context and model output.

Treat both files as potentially sensitive. They contain prompts, complete
responses, thinking when returned, model names, timestamps, request headers,
and error details. Review and redact them before attaching them to an issue.
The application does not upload logs; calls go to the local Ollama service by
default. Existing Markdown game histories may also contain information you do
not want to publish. Git ignore rules prevent ordinary accidental additions,
but forced additions can bypass them.

## Choose a path or disable logging

Use a private directory and restrictive permissions for a diagnostic session:

```sh
mkdir -p "$HOME/.local/state/ai-games"
chmod 700 "$HOME/.local/state/ai-games"
umask 077
OLLAMA_LOG_FILE="$HOME/.local/state/ai-games/tic-tac-toe-ollama.log" OLLAMA_ERROR_LOG_FILE="$HOME/.local/state/ai-games/tic-tac-toe-ollama-error.log" make run
```

On Linux, route both logs to `/dev/null` to discard new log entries:

```sh
OLLAMA_LOG_FILE=/dev/null OLLAMA_ERROR_LOG_FILE=/dev/null make run
```

An empty variable uses the normal default filename; it does not disable
logging. Disabling logs does not delete earlier files.

## Rotate logs

Stop every app instance writing to the files, move them to timestamped archive
names, and restart. Each application will recreate its log when needed:

```sh
mv tic-tac-toe-ollama.log "tic-tac-toe-ollama.log.$(date +%Y%m%d-%H%M%S)"
mv tic-tac-toe-ollama-error.log "tic-tac-toe-ollama-error.log.$(date +%Y%m%d-%H%M%S)"
```

Only move files that exist, and run these commands in the relevant working
directory. Remove old archives when no longer needed. Both default log names
and their dotted archive names are ignored by Git. A custom path is your
responsibility; keep it outside the repository or add a local ignore rule.

Pentago uses `pentago-history.md`, `pentago-ollama.log`, and
`pentago-ollama-error.log`. Histories include wins and draws, complete compound
moves, seeds, controller settings, heuristic components, and search statistics.
Its pure Ollama prompts/responses follow the same privacy and rotation guidance.

## On-screen Ollama messages

The separate conversation window is a read-only, in-memory view of the current
game's decision requests and responses. It does not read, tail, replace, or add
entries to the log files. System instructions and prompts are decoded for display;
JSON decisions/schemas are indented; native thinking and explicit `<think>` text
are separated from replies. Dedicated Cogito analysis responses appear as Thinking.
Model discovery/capability metadata remains in the logs and is omitted from the
conversation. Failed and invalid responses are visible alongside readable errors.

The original raw payloads, log schema, identifiers, paths, environment overrides,
rotation, and logging failure behavior are preserved. No additional requests are
made. Existing non-streaming responses arrive as complete messages. Clearing or
closing the on-screen transcript does not delete or change log files. As with
logs, review transcript contents before sharing copied text or screenshots.
