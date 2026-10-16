# Jogo da velha

Aplicativo GNOME em C, GTK 4 e libadwaita, com jogadores humanos ou automáticos.

## Compilar e executar

Dependências: compilador C11, Meson, Ninja, pkg-config, GLib >= 2.66,
GTK >= 4.10 e libadwaita >= 1.4, incluindo os arquivos de desenvolvimento.

No Fedora:

```sh
sudo dnf install gcc meson ninja-build pkgconf-pkg-config glib2-devel gtk4-devel libadwaita-devel
make
make run
```

Para instalar o executável, o ícone e o lançador GNOME somente para seu usuário:

```sh
make install
```

O Makefile usa Meson e Ninja. `make help` lista os alvos; `make test` testa o
motor, `make test-ui` inclui os testes gráficos e `make clean` limpa os
artefatos de compilação sem apagar o histórico. É possível escolher outro
prefixo com `make install PREFIX=/caminho` e opções Meson com
`make MESON_ARGS='-Dbuildtype=release'`.

O executável instalado fica em `~/.local/bin/jogo-da-velha`. Cada instância tem
seu próprio jogo. As escolhas da sessão não são persistidas ao fechar.

## Jogar

Escolha 0, 1 ou 2 humanos. Com um humano, escolha X ou O; X sempre começa.
Use o mouse ou as teclas 1–9, inclusive do teclado numérico com ou sem Num Lock:

```text
7 8 9
4 5 6
1 2 3
```

Somente casas vazias no turno humano aceitam entrada. O tabuleiro ocupa o
maior quadrado possível na área disponível, centralizado, sem distorção.
O slider controla a espera antes de cada jogada automática entre 0,0 e 3,0 s,
em passos de 0,1 s, inicialmente 0,5 s. Mudar seu valor reinicia uma espera
automática em andamento. Mesmo com valor zero a interface continua responsiva.

Ao vencer, uma linha verde é desenhada imediatamente sobre as três peças
alinhadas. Ao terminar, o resultado permanece por três segundos. A próxima partida começa
com X, os mesmos jogadores e o mesmo delay. “Nova configuração” abandona a
partida atual e permite mudar os jogadores.

## Estratégia

A matriz 3×3 contém 0 para vazio, 1 para X e -1 para O. Antes de cada jogada,
o avaliador considera as linhas, colunas e diagonais que passam por cada casa
vazia. Uma linha com peças de ambos os lados não pontua. Cada linha elegível
contribui com:

| Situação antes da jogada | Pontos |
| --- | ---: |
| Duas peças próprias | 1000 |
| Duas peças adversárias | 100 |
| Uma peça própria, nenhuma adversária | 10 |
| Uma peça adversária, nenhuma própria | 1 |
| Linha vazia | 0 |

A partir da segunda jogada, cada linha que passa pela casa avaliada e não
contém peças adversárias soma também **0,2 ponto**, esteja vazia ou já
contenha peças próprias. Esse fator mede as possibilidades de desenvolvimento.
Contra uma abertura no centro, os cantos mantêm 2 linhas abertas e as laterais
apenas 1. O componente aparece na coluna “Linhas abertas ×0,2” do histórico.
Como o máximo é 0,8 ponto, ele desempata os critérios anteriores antes do
aleatório. No tabuleiro vazio, esse componente é registrado como zero: as
nove casas têm o mesmo valor estratégico e apenas o aleatório escolhe a abertura.

Uma parcela aleatória em `[0; 0,1)` é acrescentada uma vez por casa.
Após a primeira jogada, o centro vazio recebe também um bônus de **5 pontos**.
Esse bônus favorece o centro na primeira resposta sem superar as prioridades
de vitória, bloqueio ou uma linha própria. Na abertura, o bônus é zero.
O histórico registra esse componente na coluna “Bônus centro”.
Além disso, cada casa é simulada para identificar garfos: duas ou mais casas
distintas onde o jogador poderá vencer no próximo turno. Um garfo próprio
recebe **+50 pontos**. Se alguma resposta adversária criar um garfo sem deixar
uma vitória imediata disponível para nós, a casa recebe **−50 pontos**, uma
única vez. Assim, ameaças que obrigam o adversário a defender também são
consideradas na prevenção de garfos. Permitir vitória adversária imediata
recebe **−500 pontos**; uma jogada que já vence não recebe penalidades.
Esses componentes e o número de respostas que criariam garfos aparecem no
histórico. A análise é limitada à jogada candidata e às respostas adversárias.
O automático escolhe o maior total. Um empate exato escolhe a primeira casa
na ordem de leitura, de cima para baixo. Os pesos garantem que vitória
imediata supere bloqueio e que bloqueio supere os critérios inferiores.
O jogador usa essa fórmula; não há busca minimax ou garantia de jogo perfeito.

## Histórico

Antes de cada jogada humana ou automática, o histórico em memória recebe o
tabuleiro anterior, os componentes e totais de todas as casas vazias e a casa
escolhida. A jogada automática usa exatamente os valores registrados.

Quando há vencedor, a partida completa é acrescentada a **`histórico.md` no
diretório de execução**, com data, hora local, fuso horário, configuração,
vencedor e tabuleiro final. O arquivo é criado se necessário. Empates e
partidas abandonadas não são gravados. O histórico da partida em memória é
zerado ao começar outra; o arquivo acumulado é preservado.

Ao abrir pelo lançador GNOME, o diretório de execução é definido pelo ambiente
de desktop. Para escolher explicitamente onde salvar, inicie pelo terminal:

```sh
cd /caminho/para/historicos
~/.local/bin/jogo-da-velha
```

Falhas de gravação aparecem na interface com um botão “Tentar gravar”. As
partidas pendentes ficam em memória durante a sessão, mesmo após começar
outra partida; novas vitórias também tentam esvaziar essa fila. Fechar o
aplicativo descarta os registros que ainda não puderam ser salvos.

## Testes

```sh
meson test -C build --print-errorlogs
```

Para testar apenas o motor, sem dependências gráficas:

```sh
meson setup build-tests -Dapp=false
meson test -C build-tests --print-errorlogs
```

Os testes verificam regras, prioridades em todas as posições alcançáveis,
componentes de pontuação e gravação acumulada do histórico. Para incluir
testes de controles, temporizadores, reinício e erros de gravação, em uma
sessão gráfica ou em um display de testes:

```sh
meson configure build -Dui_tests=true
meson test -C build --print-errorlogs
```

Os testes de interface abrem uma janela temporária e usam arquivos temporários.
Para conferir visualmente, redimensione a janela nos dois eixos, verifique o
tabuleiro em tema claro e escuro e experimente os três modos de jogadores.

## Validação nesta entrega

Os testes do motor passaram, incluindo todas as posições alcançáveis e
a recuperação após uma escrita parcial. O lançador passou pelo
`desktop-file-validate`.

Há um executável local pronto para experimentar:

```sh
./build-local/jogo-da-velha
```

Como o ambiente tem a biblioteca libadwaita instalada, mas não seus cabeçalhos
de desenvolvimento, esse executável foi compilado com declarações temporárias
das quatro funções usadas, conferidas pela introspecção instalada, e vinculado
à biblioteca real do sistema. Aplicativo e testes de interface compilaram com
`-Wall -Wextra -Wpedantic -Werror`; a análise estática do GCC também passou.
Para recompilar normalmente com Meson, instale `libadwaita-devel`.

Os testes gráficos não puderam executar neste ambiente: o sandbox bloqueou o
acesso ao display e a criação de um display Broadway. A aparência e a execução
dos testes de interface ainda precisam ser conferidas numa sessão gráfica.

## Licença

Este programa é software livre, distribuído sob os termos da GNU General Public
License, versão 3 ou (a seu critério) qualquer versão posterior. Veja `COPYING`.
