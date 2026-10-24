.DEFAULT_GOAL := all

GAMES := tic-tac-toe four-in-a-row pentalpha heptalpha peg-solitaire pentalpha-crossed-circle reversi hex tavli sokoban go pentago gomoku royal-game-of-ur senet ludus-coriovalli nine-mens-morris alquerque checkers
ifdef GAME
ifeq ($(filter $(GAME),$(GAMES)),)
$(error GAME must be tic-tac-toe, four-in-a-row, pentalpha, heptalpha, peg-solitaire, pentalpha-crossed-circle, reversi, hex, tavli, sokoban, go, pentago, gomoku, royal-game-of-ur, senet, ludus-coriovalli, nine-mens-morris, alquerque, or checkers)
endif
SELECTED_GAMES := $(GAME)
else
SELECTED_GAMES := $(GAMES)
endif

.PHONY: all build configure test test-ui test-ollama install clean run help
all: build

build configure test test-ui test-ollama install clean:
	+@set -e; for game in $(SELECTED_GAMES); do $(MAKE) -C "$$game" $@; done

run:
	+$(MAKE) -C "$(or $(GAME),tic-tac-toe)" run

help:
	@printf '%s\n' \
		'make / make test / make install / make clean: operate on all games' \
		'make test-ollama: test Ollama integration in all games' \
		'make GAME=four-in-a-row: build only Four in a Row' \
		'make run: launch Tic-Tac-Toe' \
		'make GAME=four-in-a-row run: launch Four in a Row' \
		'make GAME=pentalpha run: launch Pentalpha' \
		'make GAME=heptalpha run: launch Heptalpha' \
		'make GAME=peg-solitaire run: launch Peg Solitaire' \
		'make GAME=pentalpha-crossed-circle run: launch Pentalfa Cruzado Circulado' \
		'make GAME=reversi run: launch Reversi' \
		'make GAME=hex run: launch Hex' \
		'make GAME=tavli run: launch Tavli' \
		'make GAME=sokoban run: launch Sokoban' \
		'make GAME=go run: launch Go (go-game)' \
		'make GAME=pentago run: launch Pentago' \
		'make GAME=gomoku run: launch Go-Moku' \
		'make GAME=royal-game-of-ur run: launch The Royal Game of Ur' \
		'make GAME=senet run: launch Senet' \
		'make GAME=ludus-coriovalli run: launch Ludus Coriovalli' \
		'make GAME=nine-mens-morris run         # launch Nine Men’s Morris' \
		'make GAME=alquerque run              # launch Alquerque' \
		'make GAME=checkers run               # launch Checkers' \
		'make -C four-in-a-row help: individual project options' \
		'Build variables (PREFIX, MESON_ARGS, etc.) are forwarded to each project.'
