# Jogo da velha

Aplicativo GNOME em C, GTK 4 e libadwaita, com jogadores humanos ou automáticos.

## Compilar e executar

Dependências: compilador C11, Meson, Ninja, pkg-config, GLib >= 2.66,
GTK >= 4.10 e libadwaita >= 1.4, incluindo os arquivos de desenvolvimento.

No Fedora:

```sh
sudo dnf install gcc meson ninja-build pkgconf-pkg-config glib2-devel gtk4-devel libadwaita-devel libsoup3-devel json-glib-devel
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

Configure **X** e **O** separadamente nos seletores: **Humano**, **Posicional**,
**Minimax** ou **Ollama**. Todas as 16 combinações são permitidas, incluindo dois algoritmos
diferentes. O padrão é X humano e O posicional; X sempre começa.
Use o mouse ou as teclas 1–9, inclusive do teclado numérico com ou sem Num Lock:

```text
7 8 9
4 5 6
1 2 3
```

Somente casas vazias no turno humano aceitam entrada. O tabuleiro ocupa o
maior quadrado possível na área disponível, centralizado, sem distorção.
O slider controla a **espera mínima do turno automático** entre 0,0 e 3,0 s,
em passos de 0,1 s, inicialmente 0,5 s. O cálculo ocorre em paralelo à espera:
a jogada é aplicada quando os dois terminarem. Mudar o delay reinicia apenas
a espera, preservando o cálculo. Mesmo com valor zero a interface continua
responsiva.

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

## Minimax

O slider de profundidade aparece separadamente para cada lado selecionado
como Minimax: **1–9, padrão 9**, em passos inteiros. A profundidade inclui a
jogada candidata e é limitada às casas restantes. Com o padrão 9, analisa até
o final e joga de forma ótima; a abertura aleatória mantém todas as casas
equivalentes. Em profundidades menores, a avaliação de corte soma 1 para uma
peça e 10 para duas peças numa linha livre, descontando os mesmos valores
adversários. Profundidades menores podem permitir erros.

A busca usa poda alfa-beta, aprofundamento progressivo e uma tabela de
transposições de até 16 MiB por busca. Na raiz, cada alternativa legal recebe
um valor completo, permitindo comparar os valores do histórico. Terminais
prevalecem sobre a avaliação posicional; são preferidas vitórias mais rápidas
e derrotas mais tardias. O aleatório desempata melhores alternativas com
tolerância de 1e-9. Com o tabuleiro vazio, nenhuma busca é feita: todas as
alternativas têm valor zero e a primeira jogada é aleatória uniforme.

O jogador **Posicional** mantém a fórmula e os fatores táticos existentes.
Os cálculos automáticos usam cópias do tabuleiro e geradores aleatórios
independentes em segundo plano. Há somente uma análise ativa por aplicativo.
O indicador mostra a profundidade concluída/alvo durante a busca minimax.

Não há limite de tempo: o minimax termina a profundidade escolhida.
Valores altos podem demorar; “Nova configuração” cancela o cálculo e permite
escolher outro nível. Alterar o tipo de jogador oculta ou revela seu slider,
preservando o valor escolhido. O reinício automático conserva tipos e níveis;
fechar a janela cancela as tarefas, e resultados antigos não podem executar
jogadas numa partida nova.

## Histórico

Antes de cada jogada humana ou automática, o histórico em memória recebe o
tabuleiro anterior, os componentes e totais de todas as casas vazias e a casa
escolhida. O Posicional usa exatamente esses valores. Para Minimax, uma seção adicional
registra a avaliação de cada casa legal, profundidade solicitada/concluída,
nós visitados, duração e casa escolhida. O título identifica o controlador;
a configuração da partida inclui a profundidade de cada jogador Minimax.

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

## Localização no repositório

Este aplicativo fica em `jogo-da-velha/`. Execute os comandos acima dentro
dessa pasta ou use `make run` na raiz. O Makefile da raiz também aceita
`make GAME=jogo-da-velha test` e `make GAME=jogo-da-velha install`.
Os builds anteriores à reorganização, na raiz, não são reutilizados.
Históricos antigos permanecem no local original; os comandos Make agora
executam o aplicativo dentro desta pasta.


## Jogador Ollama

Cada jogador pode selecionar **Ollama**, além de Humano, Posicional ou Minimax.
O seletor **Modelo** aparece somente para Ollama e é independente para os dois
jogadores. A lista é consultada em segundo plano, ordenada por nome, e pode ser
recarregada com **Atualizar modelos**. A consulta usa `GET /api/tags`, a API
correspondente à lista de modelos disponíveis no serviço consultado por `ollama list`.
A primeira escolha é o primeiro modelo da lista; atualizações preservam cada
seleção enquanto o modelo continuar disponível. Sem serviço ou modelos, o início
com Ollama fica desabilitado; os demais jogadores continuam disponíveis.

O serviço deve estar ativo em **http://127.0.0.1:11434**. Instale um modelo pelo
próprio Ollama antes de jogar; o aplicativo não baixa modelos nem inicia o servidor.
Se necessário, use `ollama serve` e, por exemplo, `ollama pull llama3.2:3b`.
Os modelos selecionados continuam nas partidas seguintes durante a sessão.
Modelos que não aceitem geração de texto ou a resposta estruturada solicitada
produzirão um erro de jogada, sem substituição por outro algoritmo.

Cada turno envia as regras, o tabuleiro com símbolos e coordenadas, a identificação do jogador e as jogadas
legais por `POST /api/generate`, com resposta JSON estruturada. O modelo escolhe
inclusive a primeira jogada. As avaliações posicionais continuam no histórico,
mas não são enviadas como sugestões ao modelo. A resposta é validada novamente
pelo motor: posições ocupadas e colunas cheias não são aceitas.

A interface permanece disponível durante a consulta. O delay continua sendo o
tempo mínimo do turno, sobreposto à geração. A lista tem prazo total de 100 s;
cada jogada, 1.800 s (30 minutos), incluindo o carregamento do modelo. Falhas de conexão,
timeout e respostas inválidas **pausam a partida**, preservando o tabuleiro.
Use **Tentar novamente** para repetir a mesma posição, ou **Nova configuração**
para abandonar a partida. Não existe jogada automática de reserva. Fechar ou
reconfigurar cancela a solicitação e descarta resultados atrasados.

O histórico identifica o modelo, a jogada, a duração e tentativas com erro.
Mantém-se a política de salvar apenas partidas com vencedor e reiniciar após
três segundos. Empates e partidas abandonadas não são gravados.

O build dos aplicativos agora também requer **libsoup 3** e **JSON-GLib**:

```sh
# Fedora
sudo dnf install libsoup3-devel json-glib-devel
```

`make test` continua testando os motores apenas com GLib. `make test-ollama`
compila o aplicativo e executa os testes do cliente, sem exigir display nem um
Ollama real: há validação de protocolo sem rede e testes HTTP com servidor
simulado. Os testes HTTP são marcados como ignorados se o ambiente impedir
sockets locais. `make test-ui` inclui as 16 combinações de jogadores, escolhas
independentes de modelos, atualização da lista, falhas, repetição e cancelamento.
Os testes de interface requerem uma sessão gráfica.


### Registro completo de comunicação

Todas as consultas de modelos e solicitações de jogada são acrescentadas ao
arquivo **`ollama.log` no diretório de execução**, independentemente de vitória,
empate, erro ou abandono. O arquivo não é zerado entre partidas ou execuções.
A variável `OLLAMA_LOG_FILE` permite escolher outro caminho.

Cada linha é um objeto JSON com data/hora e fuso, jogo, processo, identificador
da consulta, evento, método, URL, headers disponíveis, tempo decorrido e corpo
completo enviado ou recebido. Requisições, respostas, erros, cancelamentos e
timeouts compartilham o identificador da consulta. Corpos inválidos para UTF-8
são preservados em Base64. O corpo recebido é registrado antes da validação,
incluindo respostas HTTP de erro e JSON inválido.

O campo `thinking` e qualquer texto `<think>` são preservados quando enviados
pelo Ollama. A gravação do log registra o conteúdo que o servidor efetivamente retorna.
A opção “Raciocínio ampliado” controla a solicitação de raciocínio descrita abaixo. Como a integração usa `stream: false`, a resposta
completa é registrada ao terminar a consulta; interrupções podem não fornecer
um corpo parcial. Uma falha de gravação é informada no terminal e não interrompe
a partida. O arquivo é ignorado pelo Git.

Para ler o `thinking` retornado nas respostas JSON, com `jq`:

```sh
jq -r 'select(.event == "response") | .body | fromjson | .thinking // empty' ollama.log
```


### Força do jogador Ollama

O prompt agora identifica as peças próprias com **Y**, as adversárias com **A**
e casas vazias com **.**, numera linhas e colunas e informa as coordenadas de
cada jogada legal. No Lig-4, informa a linha em que o disco cairá, respeitando
a gravidade; no jogo da velha, mantém a numeração do teclado numérico.
O modelo recebe instruções para verificar vitórias e respostas adversárias
antes de considerar garfos e planos de longo prazo.

Cada jogador Ollama tem duas opções independentes, mantidas entre partidas:

- **Assistência tática**: desmarcada por padrão, mantendo o modo **puro**.
  Quando marcada, o motor restringe as escolhas às vitórias imediatas, se
  existirem. Caso contrário, restringe às jogadas que não permitem uma vitória
  adversária na resposta seguinte, se houver alguma. Se todas permitirem uma
  derrota imediata, todas as escolhas legais continuam disponíveis. São fatos
  táticos verificados por simulação das regras; as pontuações do Posicional e
  do Minimax não participam da escolha. O modelo continua escolhendo a jogada.
- **Raciocínio ampliado**: marcada por padrão. Para modelos que anunciam
  controles em `/api/show`, solicita `true`, o nível `high` quando disponível,
  ou um valor padrão anunciado e suportado. Sem esses metadados, mantém o
  comportamento padrão do modelo, evitando parâmetros incompatíveis.
  Desmarcar também mantém o comportamento padrão, que pode incluir raciocínio.

Para modelos com nome `cogito` ou `cogito:tag` (inclusive em namespace), a
ampliação usa o protocolo do Cogito v1: primeiro solicita análise com
`Enable deep thinking subroutine.` no sistema, sem restringir a geração ao
esquema JSON. Depois envia essa análise ao mesmo modelo para selecionar a
jogada em uma segunda consulta, com o esquema JSON. O reconhecimento pelo
nome não abrange modelos renomeados arbitrariamente nem Cogito 2.1.
As consultas compartilham o prazo total de 1.800 segundos (30 minutos) e o cancelamento do
turno. Uma falha em qualquer etapa pausa a partida, sem substituição automática.

O motor valida também o filtro assistido: uma resposta legal que ignore esse
filtro é rejeitada e pode ser repetida. O histórico identifica modo puro ou
assistido, alternativas permitidas, solicitação de raciocínio, estratégia usada
e duração total. `ollama.log` inclui as consultas de metadados, a análise e a
decisão final, além das respostas completas.

Os testes do Lig-4 reproduzem as ameaças imediatas das derrotas do Cogito de
18:43:39 e 18:43:54 em 2026-10-16: o modo assistido exige os bloqueios nas colunas 4 e 2,
respectivamente. Na derrota de 18:43:47, já existiam duas ameaças simultâneas
antes da última decisão: o filtro reconhece a ausência de defesa imediata.
Isso não garante evitar garfos preparados em turnos anteriores ou vencer o
Posicional. Os casos também cobrem espelhamento e troca de cores.

Referências do protocolo: [thinking no Ollama](https://docs.ollama.com/capabilities/thinking)
e [raciocínio ampliado do Cogito](https://ollama.com/library/cogito:3b).
