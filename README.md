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
sudo dnf install gcc meson ninja-build pkgconf-pkg-config glib2-devel gtk4-devel libadwaita-devel libsoup3-devel json-glib-devel
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

Cada lado tem um seletor **Humano / Posicional / Minimax / Ollama**, permitindo todas
as combinações. Cada jogador Minimax tem profundidade própria: 1–9 (padrão 9)
no jogo da velha e 1–12 (padrão 6) no Lig-4. O padrão é primeiro lado humano
e segundo posicional. A busca roda em segundo plano e conclui a profundidade
escolhida; “Nova configuração” cancela a análise.

O delay de 0–3 segundos, em passos de 0,1 segundo, é a espera mínima do turno:
análise e temporizador começam juntos. O reinício três segundos após cada
resultado conserva os controladores, suas profundidades e o delay. A abertura
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


Os dois jogos também oferecem **Ollama** como jogador, com modelo independente
por participante e atualização da lista do serviço local (`127.0.0.1:11434`).
O build gráfico requer libsoup 3 e JSON-GLib, além das dependências anteriores.
Use `make test-ollama` para testar o cliente; `make test` mantém os testes dos
motores sem dependências gráficas ou de rede. Consulte o README de cada jogo
para configuração, tratamento de erros e detalhes do histórico.

Toda a comunicação com Ollama é acrescentada a `ollama.log` no diretório de
execução, incluindo prompts, respostas completas e `thinking` quando retornado
pelo modelo. O arquivo usa JSON Lines; `OLLAMA_LOG_FILE` permite outro caminho.

O jogador Ollama oferece **Assistência tática** opcional (desmarcada por padrão)
e **Raciocínio ampliado** (marcado por padrão), configurados por participante.
O histórico distingue os modos puro e assistido. O prompt identifica peças,
coordenadas e locais de queda; o Cogito v1 pode analisar antes de responder em
JSON. Consulte os READMEs dos jogos para os controles e limites dessas opções.
