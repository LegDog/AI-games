.DEFAULT_GOAL := all

MESON ?= meson
BUILD_DIR ?= build
TEST_BUILD_DIR ?= build-tests
UI_BUILD_DIR ?= build-ui
PREFIX ?= $(HOME)/.local
MESON_ARGS ?=

.PHONY: all configure build run test test-ui test-ollama install clean help

all: build

configure:
	@if [ -f "$(BUILD_DIR)/meson-private/coredata.dat" ]; then \
		$(MESON) setup --reconfigure "$(BUILD_DIR)" --prefix="$(PREFIX)" -Dapp=true -Dui_tests=false $(MESON_ARGS); \
	else \
		$(MESON) setup "$(BUILD_DIR)" --prefix="$(PREFIX)" -Dapp=true -Dui_tests=false $(MESON_ARGS); \
	fi

build: configure
	$(MESON) compile -C "$(BUILD_DIR)"

run: build
	"$(abspath $(BUILD_DIR))/connect-4"

# Engine tests do not require GTK or libadwaita development packages.
test:
	@if [ -f "$(TEST_BUILD_DIR)/meson-private/coredata.dat" ]; then \
		$(MESON) setup --reconfigure "$(TEST_BUILD_DIR)" -Dapp=false -Dui_tests=false; \
	else \
		$(MESON) setup "$(TEST_BUILD_DIR)" -Dapp=false -Dui_tests=false; \
	fi
	$(MESON) test -C "$(TEST_BUILD_DIR)" --print-errorlogs

test-ui:
	@if [ -f "$(UI_BUILD_DIR)/meson-private/coredata.dat" ]; then \
		$(MESON) setup --reconfigure "$(UI_BUILD_DIR)" -Dapp=true -Dui_tests=true $(MESON_ARGS); \
	else \
		$(MESON) setup "$(UI_BUILD_DIR)" -Dapp=true -Dui_tests=true $(MESON_ARGS); \
	fi
	$(MESON) test -C "$(UI_BUILD_DIR)" --print-errorlogs

test-ollama: build
	$(MESON) test -C "$(BUILD_DIR)" --print-errorlogs ollama ollama-log

install: build
	DESTDIR="$(DESTDIR)" $(MESON) install -C "$(BUILD_DIR)"

clean:
	@for dir in "$(BUILD_DIR)" "$(TEST_BUILD_DIR)" "$(UI_BUILD_DIR)"; do \
		if [ -f "$$dir/meson-private/coredata.dat" ]; then \
			$(MESON) compile -C "$$dir" --clean || exit $$?; \
		fi; \
	done

help:
	@printf '%s\n' \
		'make             Compilar o aplicativo' \
		'make run         Compilar e executar' \
		'make test        Testar o motor sem dependências gráficas' \
		'make test-ollama  Testar o cliente Ollama sem precisar de display ou modelos' \
		'make test-ui     Testar também a interface (requer display)' \
		'make install     Instalar em PREFIX (padrão: ~/.local)' \
		'make clean       Limpar os artefatos gerenciados pelo Meson' \
		'Variáveis: PREFIX, BUILD_DIR, TEST_BUILD_DIR, UI_BUILD_DIR, MESON_ARGS'
