.DEFAULT_GOAL := all

GAMES := tic-tac-toe four-in-a-row
ifdef GAME
ifeq ($(filter $(GAME),$(GAMES)),)
$(error GAME must be tic-tac-toe or four-in-a-row)
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
		'make / make test / make install / make clean: operate on both games' \
		'make test-ollama: test Ollama integration in both games' \
		'make GAME=four-in-a-row: build only Four in a Row' \
		'make run: launch Tic-Tac-Toe' \
		'make GAME=four-in-a-row run: launch Four in a Row' \
		'make -C four-in-a-row help: individual project options' \
		'Build variables (PREFIX, MESON_ARGS, etc.) are forwarded to each project.'
