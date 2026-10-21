# Ollama reasoning harness

All thirteen games offer a per-player **Reasoning harness** option when Ollama
is selected. It is **off by default**. The selected model makes two successive
requests for each decision:

1. **Propose:** examine the rules, original board, side to move and complete
   legal choices; recommend one decision with a short outward rationale.
2. **Review:** independently examine the same original position and the
   fallible proposal, then return the final decision in the existing strict
   game-specific JSON format. The reviewer may change the proposed move.

Both stages use the same selected Ollama model. The host supplies no heuristic,
search result, tactical classification, or simulated successor. Existing
**Tactical/Search/Backtracking assistance** and the harness are mutually
exclusive: enabling either turns the other off. API callers supplying both
flags are normalized to unassisted harness mode before legal choices are built.

**Extended thinking** remains independent. When requested, advertised native
thinking controls apply to both stages. Without that option, model defaults
remain in effect. Legacy `cogito`/`cogito:<tag>` models receive their special
thinking instruction during Propose; Review produces the final structured
answer. This uses two generation requests, not an additional Cogito analysis
call. Native thinking fields and inline `<think>` blocks are displayed but are
not forwarded as the proposal to Review. Models without a native thinking mode
can still use the harness.

The proposal asks for at most 1,200 characters; the host enforces a hard
16 KiB limit on the outward answer before reuse. Empty, incomplete, oversized,
or thinking-only proposals stop the decision. The final JSON must pass the
same completion, structure, and legality checks as direct Ollama play.

All stages, thinking discovery and legal-choice generation share the existing
turn deadline and cancellation token. Tavli applies the pair to each decision
needed to assemble a turn, retaining a single deadline for the entire turn.
Forced automatic actions retain their existing handling. Harness-enabled
Tic-Tac-Toe asks the model for the opening too; its existing local opening
shortcut is retained when the option is off.

A failure exposes the normal error/Retry flow. There is no automatic repair
request, proposal fallback, engine substitution, persistent memory, or automatic
prompt modification. The direct path is retained when the option is off.

## Messages, logs, and histories

The messages window labels each exchange **Propose** or **Review**, retaining the
Sent, Thinking, Received and error colors, selection/copying and scrolling.
Outward proposal answers appear as Received; native/inline reasoning remains
Thinking. Calls remain non-streaming, so messages arrive after each response
completes. Closing or reopening the window does not change the decision.

Existing diagnostic files and raw payload fields are retained. JSONL entries
add `harness`, `harness_stage` (when applicable), generation attempts and completed
generations. Counts in a response log precede its completion accounting; the
operation result/history contains the final totals. Histories identify
`propose-review` or `off`, generation counts, reported prompt/output tokens,
elapsed time and the game's usual final move record. Request failures and
cancellation still use the existing operation/error identifiers.

The shared implementation is native C in `tools/ollama-harness.*`. Each game
continues to own position prompts, schemas, transport/logging and final move
validation. It is inspired by the harness-design ideas in Google's
[Harness²](https://github.com/google-research/harness2), but does not depend on
that framework or copy its code. This is bounded per-decision self-review, not
Harness²'s recursive propose/probe/compose optimization or retained learning.

## Compare a model before claiming improvement

`make GAME=gomoku` builds an opt-in comparison executable. No model-backed
requests are part of required CI.

```sh
# List fixed fixtures without contacting Ollama:
gomoku/build/ollama-harness-eval --list

# Replace installed-model:tag with an installed model's exact name:
mkdir -p results
gomoku/build/ollama-harness-eval --model=installed-model:tag --thinking \
  --seed=42 --temperature=0 --timeout-ms=60000 \
  --revision="$(git rev-parse HEAD)" > results/gomoku-harness.jsonl
```

Each game's `build/ollama-harness-eval` compares both modes on the **same** three
public, deterministic fixtures, without mutating the source position. Use
`--fixture=0`, `1`, or `2` to select one. `--url` overrides `OLLAMA_URL` and the
normal localhost default. Each mode receives the same total deadline,
thinking request, generation seed and temperature. Model determinism still
depends on Ollama and the model; a fixed seed is not a guarantee. Direct runs
precede harness runs, so model warm-up can affect timings; compare repeated,
warmed runs rather than interpreting one pair as a latency benchmark.

| Game | Fixture 0 | Fixture 1 | Fixture 2 |
|---|---|---|---|
| Tic-Tac-Toe | Empty | Immediate winning opportunity | Opponent threatens an immediate win |
| Four in a Row | Empty | Vertical winning opportunity | Opponent threatens a vertical win |
| Pentalpha, Heptalpha, Crossed Circle | Empty | First legal placement played | First legal placements played up to three |
| Peg Solitaire | English, any-hole goal | Triangle, any-hole goal | English after first legal jump |
| Reversi | Opening | First legal moves played twice | First legal moves played four times |
| Hex | 5×5 with pie rule | Opening stone played; swap available | 3×3 with pie rule |
| Tavli | Backgammon pre-roll | Running doubles requiring two uses | Backgammon cube response |
| Sokoban | Built-in level 0 | Built-in level 1 | Built-in level 2 |
| Go | Empty, komi 7.5 | Two stones played | One pass played |
| Pentago | Empty | Placement-only winning opportunity | Two complete rotating turns played |
| Go-Moku | Empty | Immediate winning opportunity | Opponent threatens an immediate win |

JSONL rows contain model, revision, fixture, mode, settings, generation counts,
legality/failure, elapsed microseconds, reported prompt/output token totals,
choice and error details. `token_counts_available` distinguishes reported counts
from missing server metrics. Puzzle choices use move indices where the game
already uses those IDs; keep the game and revision with exported records.
`immediate_success` measures an immediate win/solution for the current actor;
Go reports null because this small fixture suite does not adjudicate play quality.
An evaluation exits 1 if any decision fails or is illegal, 2 for invalid CLI
arguments, and 0 otherwise; failed decisions still produce rows.

The fixtures are deliberately small regression samples, not a win-rate or
expert-strength benchmark. Compare legality/failure rates, latency and tokens
as well as immediate outcomes. Add broader held-out positions, repeated runs,
both sides and full games before concluding that a model plays better. Review
can reinforce a wrong proposal; it is not guaranteed to improve decisions.

## Automated verification

`make test-ollama` exercises every game's real adapter against local mock
servers, including native thinking, Cogito, compound decisions, complete legal
choices, malformed finals, invalid proposals and cancellation. The Hex build
also runs shared scope/deadline tests. `make test-ui` checks controls, panel
stability, stale-worker handling and the messages window.

`python3 scripts/test-ollama-harness-eval.py` checks matched evaluation settings,
fixture listing, JSONL reports, counts and errors across all thirteen games
using a deterministic mock service. It needs built application targets, local
socket access, and no model downloads or external service.
