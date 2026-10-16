# AI-games

Dois aplicativos GNOME independentes em C, GTK 4 e libadwaita, sob a licença
GPL-3.0-or-later:

- [Jogo da velha](jogo-da-velha/README.md): matriz 3×3, mouse e teclado numérico.
- [Lig-4](connect-4/README.md): tabuleiro 7×6 com gravidade, mouse e teclas 1–7.

Cada diretório contém seu próprio motor, interface, testes, Makefile, projeto
Meson, recursos de instalação e cópia da licença. Não é necessário compilar
um aplicativo para usar o outro.

## Dependências e compilação

No Fedora:

```sh
sudo dnf install gcc meson ninja-build pkgconf-pkg-config glib2-devel gtk4-devel libadwaita-devel
make
```

São necessários C11, GLib >= 2.66, GTK >= 4.10 e libadwaita >= 1.4.
As bibliotecas e os arquivos de desenvolvimento são os instalados no sistema.

## Comandos da raiz

```sh
make                              # compilar os dois aplicativos
make run                          # abrir o jogo da velha
make GAME=connect-4 run            # abrir o Lig-4
make GAME=connect-4                # compilar somente o Lig-4
make test                         # testes dos dois motores, sem display
make GAME=connect-4 test           # testes de um motor
make test-ui                      # incluir testes gráficos; requer display
make install                      # instalar ambos em ~/.local
make install PREFIX=/outro/local  # escolher outro prefixo
make clean                        # limpar artefatos, preservando históricos
```

Os alvos `build`, `configure`, `test`, `test-ui`, `install` e `clean` atuam nos
dois projetos, ou somente naquele indicado por `GAME`. `run` escolhe o jogo
da velha quando `GAME` não é informado. As opções `PREFIX`, `MESON_ARGS` e
`DESTDIR` são repassadas aos Makefiles individuais. Caminhos relativos de
build são relativos à pasta de cada aplicativo.

Também é possível usar `make -C connect-4` ou entrar no diretório do jogo e
executar `make`, `make run`, `make test`, `make test-ui`, `make install` e
`make clean`. Para uma compilação otimizada, use
`make MESON_ARGS='-Dbuildtype=release'`.

## Partidas e históricos

Ambos permitem 0, 1 ou 2 humanos, delay automático de 0–3 segundos em passos
de 0,1 segundo e reinício três segundos após cada resultado. A abertura
trata todas as jogadas iniciais como equivalentes e usa somente o aleatório.

Antes de cada jogada humana ou automática, os componentes da avaliação são
registrados em memória. Partidas com vencedor são acrescentadas ao histórico,
com data e hora; empates e partidas abandonadas não são gravados.

O jogo da velha usa `histórico.md`; o Lig-4 usa `histórico-connect-4.md`.
Os arquivos são relativos ao diretório de execução. `make run` e
`make GAME=connect-4 run` executam dentro da pasta do respectivo aplicativo.
Iniciados diretamente ou pelo lançador GNOME, usam o diretório de trabalho
fornecido pelo terminal ou desktop.

Os diretórios de build antigos na raiz não são reutilizados após a separação.
Históricos existentes foram preservados em seu local original. Novos builds
ficam em cada projeto; builds e históricos locais são ignorados pelo Git.
