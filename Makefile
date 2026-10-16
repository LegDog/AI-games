.DEFAULT_GOAL := all

GAMES := jogo-da-velha connect-4
ifdef GAME
ifeq ($(filter $(GAME),$(GAMES)),)
$(error GAME must be jogo-da-velha or connect-4)
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
	+$(MAKE) -C "$(or $(GAME),jogo-da-velha)" run

help:
	@printf '%s\n' \
		'make / make test / make install / make clean: operate on both games' \
		'make test-ollama: test Ollama integration in both games' \
		'make GAME=connect-4: build only Connect Four' \
		'make run: launch Tic-Tac-Toe' \
		'make GAME=connect-4 run: launch Connect Four' \
		'make -C connect-4 help: individual project options' \
		'Build variables (PREFIX, MESON_ARGS, etc.) are forwarded to each project.'
