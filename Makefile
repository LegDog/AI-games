.DEFAULT_GOAL := all

GAMES := jogo-da-velha connect-4
ifdef GAME
ifeq ($(filter $(GAME),$(GAMES)),)
$(error GAME deve ser jogo-da-velha ou connect-4)
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
		'make / make test / make install / make clean: atuar nos dois jogos' \
		'make test-ollama: testar a integração Ollama nos dois jogos' \
		'make GAME=connect-4: compilar apenas o Lig-4' \
		'make run: executar o jogo da velha' \
		'make GAME=connect-4 run: executar o Lig-4' \
		'make -C connect-4 help: opções do projeto individual' \
		'Variáveis de build (PREFIX, MESON_ARGS, etc.) são repassadas aos projetos.'
