# Games and future additions

- [x] **Pentalfa Cruzado Circulado** — [Implemented](pentalpha-crossed-circle/README.md): 36 points, 35 pieces, closed-circle paths, iterative Search, and Ollama.
- [x] **Peg Solitaire (Resta Um)** — [Implemented](peg-solitaire/README.md).
  English and triangular boards, bounded Backtracking/A*, and Ollama.
  Jump over and remove pegs, aiming to leave one, with animated jumps and captures.
  [Rules](https://www.chiark.greenend.org.uk/~sgtatham/puzzles/doc/pegs.html)
- [x] **Reversi / Othello** — [Implemented](reversi/README.md).
  Two-player disc-flipping game. Compare positional
  scoring, Minimax, Monte Carlo tree search, and Ollama; animate disc flips.
  [Rules](https://www.worldothello.org/about/about-othello/othello-rules/official-rules/english)
- [x] **Hex** — [Implemented](hex/README.md): 3×3, 5×5, and 7×7 boards,
  optional pie rule, connection heuristic, Minimax, MCTS, and Ollama.
  [Rules](https://webhomes.maths.ed.ac.uk/~csangwin/hex/index.html)
- [x] **Sokoban** — [Implemented](sokoban/README.md): eight original levels,
  XSB import, Human, Random, Greedy Best-first, A*, and pure Ollama;
  optional deadlock pruning, minimum-push A*, and animated walking/pushes.
  [Rules](https://sokoban.jp/rule.html)
- [x] **Tavli** — [Implemented](tavli/README.md): **Portes**, **Plakoto**,
  **Fevga**, **Backgammon**, and custom **Running**; fixed/traditional matches,
  Backgammon cube, race/blocking heuristic, Expectiminimax, stochastic MCTS,
  and pure Ollama. Running follows the supplied and clarified rules.
  Research reference: Gerald Tesauro’s **TD-Gammon (1992)** used a neural network
  with temporal-difference learning and self-play to reach master-level play,
  demonstrating reinforcement learning on stochastic, dice-based boards.
- [x] **Go** — [Implemented](go/README.md): 9×9, Chinese-style area scoring,
  no suicide, positional superko, configurable komi, Human, Random, Heuristic,
  MCTS and pure Ollama; animated captures, score overlay and move history.
  Consider 13×13 and 19×19 later.
  [Rules overview](https://www.britgo.org/gorules)
- [x] **Pentago** — [Implemented](pentago/README.md): standard 6×6 rotating
  board, plain Minimax versus alpha-beta, weak/strong presets and custom limits,
  Human, Random, Heuristic, pure Ollama, animated turns, and replayable tournaments.
- [x] **The Royal Game of Ur** — [Implemented](royal-game-of-ur/README.md): seven-piece
  race, four binary dice, safe central rosette, captures, exact bearing off,
  Human, Random, Heuristic, Expectiminimax, pure Ollama, and seeded experiments.
- [x] **Senet** — [Implemented](senet/README.md): modern five-piece teaching
  reconstruction, throwing sticks, protected neighbors, exchange captures,
  special houses, Human, Random, Heuristic, Expectiminimax, and pure Ollama.
- [ ] **Ludus Coriovalli / Roman Blocking Games**.
- [ ] **Nine Men’s Morris**.
- [ ] **Alquerque**.
- [ ] **Checkers (Draughts)**.
- [x] **Go-Moku (Five in a Row)** — [Implemented](gomoku/README.md): freestyle
  15×15, five or more wins, Human, Random, Heuristic, plain Minimax, alpha-beta,
  pure Ollama, animated placements, and replayable tournaments.
- [ ] **Fox and Geese**, including traditional variants such as **Halatafl**.
- [ ] **Tafl Games (Viking Chess / Hnefatafl)**.
- [ ] Kaooa (The Vulture and the Crows)
