# Lig-4

Aplicativo GNOME independente em C, GTK 4 e libadwaita, sob GPL-3.0-or-later.

## Compilar e executar

Requer compilador C11, Meson, Ninja, pkg-config, GLib >= 2.66, GTK >= 4.10
e libadwaita >= 1.4, com seus arquivos de desenvolvimento.

```sh
make
make run
make test
make install
```

Os comandos acima são executados nesta pasta. Na raiz do repositório, use
`make GAME=connect-4 run`. `make install` instala executável, ícone e lançador
em `~/.local`; `PREFIX=/caminho` e `DESTDIR=/pasta` permitem escolher prefixo
ou fazer uma instalação de preparação. A identidade GNOME é
`io.github.aigames.Connect4`, o executável é `connect-4` e o nome exibido é
**Lig-4**.

## Jogar

O tabuleiro tem 7 colunas e 6 linhas. Clique na coluna ou use as teclas **1–7**,
inclusive do teclado numérico com ou sem Num Lock. A peça cai na casa vazia
mais baixa. Colunas cheias não aceitam jogadas e não alteram o turno.

Configure **Vermelho** e **Amarelo** separadamente nos seletores: **Humano**,
**Posicional**, **Minimax** ou **Ollama**. Todas as 16 combinações são permitidas.
O padrão é vermelho humano e amarelo posicional; vermelho sempre começa. Os discos vermelhos contêm uma cruz e os amarelos
um anel para permitir sua identificação além da cor. Quatro ou mais peças
iguais em sequência horizontal, vertical ou diagonal vencem. Todos os
alinhamentos vencedores são destacados imediatamente. Tabuleiro cheio sem
vencedor é empate.

O tabuleiro mantém proporção 7:6 e casas quadradas, centralizado na maior
área disponível abaixo dos números das colunas. O slider define a **espera mínima do turno automático**
de 0,0 a 3,0 s, em passos de 0,1 s, inicialmente 0,5 s. A análise começa
junto com a espera; a peça é aplicada quando ambas terminarem. Alterá-lo
reinicia apenas a espera, sem refazer o cálculo. O valor zero mantém a
interface responsiva entre jogadas.

O resultado permanece por 3 segundos e outra partida começa, com os mesmos
jogadores e delay. “Nova configuração” cancela a partida e seus temporizadores.
As escolhas não são persistidas ao fechar.

## Avaliação estratégica

O motor usa matriz `int[6][7]`, com 0 para vazio, 1 para vermelho e -1 para
amarelo. Avalia somente as jogadas legais por coluna, simulando a gravidade.
Com tabuleiro vazio, todos os fatores estratégicos são zero e apenas o
aleatório decide entre as sete colunas.

Depois da abertura, cada jogada é simulada e **todas as 69 janelas de quatro
casas do tabuleiro resultante** são avaliadas. Janelas com peças dos dois
jogadores não contribuem. Para cada lado, janelas com uma, duas, três ou quatro
peças têm pesos **10, 100, 1000 e 10000**. Janelas completamente vazias são
contadas no histórico, mas não favorecem nenhum lado.

A contribuição de cada janela é dividida por **1 + número de apoios ausentes**.
São contadas as casas vazias abaixo das casas vazias da janela, excluindo as
que fazem parte da própria janela e sem contar o mesmo apoio duas vezes.
Assim, uma sequência horizontal ou diagonal suspensa vale menos que outra
acessível; os espaços de uma janela vertical podem apoiar uns aos outros
conforme a sequência é desenvolvida.

O valor global da posição, do ponto de vista de quem vai jogar, é:

```text
posicional = 0,1 × (potencial próprio − 1,25 × potencial adversário)
```

A defesa recebe peso ligeiramente maior. A escala mantém o componente global
abaixo dos fatores táticos, inclusive quando a melhor jogada apenas reduz uma
posição desfavorável. No histórico, os potenciais dos dois lados e as contagens
de janelas sem/com apoios externos ausentes permitem conferir esse cálculo.

São acrescentados estes componentes táticos, aplicados uma vez por coluna:

| Situação | Pontos |
| --- | ---: |
| Vitória imediata | +1.000.000.000 |
| Bloqueio de uma vitória disponível antes da jogada | +10.000.000 |
| Criação de garfo ou ameaça empilhada própria | +100.000 |
| Alguma resposta adversária cria garfo ou ameaça empilhada relevante | −100.000 |
| Permite vitória adversária imediata | −100.000.000 |
| Sequência própria de vitória forçada demonstrada | +1.000.000 |
| Sequência adversária de vitória forçada demonstrada | −20.000.000 |

Jogadas que já vencem não recebem penalidades. Um garfo tem pelo menos duas
**colunas distintas onde uma peça pode realmente cair e vencer**. Uma ameaça
empilhada ocorre quando bloquear uma vitória dá apoio a outra casa de vitória
na **mesma coluna**. O motor simula esse bloqueio e confere se o defensor pode
vencer imediatamente em vez de defender. O bônus é aplicado uma vez, mesmo
quando uma jogada cria tanto um garfo quanto um empilhamento.

A análise das respostas adversárias também inclui empilhamentos. Ela ignora
uma resposta perigosa que deixe nossa vitória imediata disponível. Casas sem
apoio não são tratadas como vitórias imediatas; os casos em que uma jogada
fornece esse apoio são detectados pela simulação.

O motor também procura **cadeias contínuas de ameaças e bloqueios obrigatórios**.
Depois da jogada candidata, explora ataques que criem uma vitória imediata
para o próximo turno. O defensor deve bloquear, ou vencer antes; após o
bloqueio, a análise continua com o próximo ataque. Duas ameaças em colunas
distintas encerram a prova se o defensor não puder vencer imediatamente.
Isso permite reconhecer preparações de garfos ao longo de vários bloqueios.

A análise tem limite de **12 meias-jogadas** e **1024 nós por tentativa**,
com orçamento próprio para a prova adversária e, quando necessário, a própria.
Vitórias, garfos e empilhamentos já demonstrados reaproveitam as provas curtas.
Os novos bônus são adicionais aos componentes anteriores; a vitória imediata
continua tendo prioridade. A abertura continua usando somente o aleatório.
Quando a busca contínua não encontra uma prova, uma extensão permite até
**duas jogadas preparatórias do atacante**, com **4096 nós por tentativa** e
o mesmo limite de 12 meias-jogadas. Essas jogadas devem criar uma casa que
completaria quatro peças ou fornecer apoio abaixo de uma dessas casas.
Se não houver ameaça imediata, **todas as respostas legais do defensor**
precisam levar à vitória do atacante para que a derrota seja penalizada.
Isso antecipa ataques com preparação de garfos e empilhamentos, sem assumir
que o adversário colaborará fornecendo apoio.

O histórico registra vitória/derrota com preparação, além dos indicadores de
vitória/derrota forçada e de limite de nós atingido. As preparações usam os
mesmos bônus e penalidades das outras provas, sem pontuação duplicada.
Não encontrar uma cadeia não demonstra segurança: outras preparações e
sequências além dos limites continuam fora dessa análise.

Uma parcela aleatória em `[0; 0,1)` é somada por coluna. O maior total vence;
em empate exato, prevalece a coluna mais à esquerda. Os pesos estão em
constantes nomeadas no motor. A avaliação posicional é complementada pela análise tática e pelas cadeias
forçadas descritas acima; não há busca geral minimax nem garantia de jogo perfeito.

## Minimax

O slider de profundidade aparece separadamente para cada lado selecionado
como Minimax: **1–12, padrão 6**, em passos inteiros. Cada nível é uma jogada
individual, incluindo a candidata, e o nível efetivo é limitado às casas
restantes. A avaliação das folhas usa o valor global do tabuleiro com os
descontos de acessibilidade descritos acima, da perspectiva do jogador da
raiz. Ela não executa a análise de cadeias forçadas do jogador Posicional.

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

Antes de cada jogada, inclusive humana, o histórico recebe tabuleiro anterior,
coluna e linha de queda, componentes posicionais, fatores táticos, aleatório
e total de cada coluna legal. O Posicional usa exatamente esses valores. No turno Minimax, uma tabela
adicional registra os valores da busca usados na escolha, profundidade
solicitada/concluída, nós visitados, duração e a coluna escolhida. O título de
cada jogada e a configuração identificam Humano, Posicional, Minimax ou Ollama.
Linhas são numeradas de 1 a 6 de cima para baixo. O tabuleiro textual usa
V para vermelho e A para amarelo.

Vitórias são acrescentadas a **`histórico-connect-4.md` no diretório de
execução**, com data, hora local, fuso, configuração, vencedor e tabuleiro
final. Empates e partidas abandonadas são descartados. O histórico em memória
é limpo a cada nova partida; o arquivo acumulado é preservado.

Erros de gravação aparecem com um botão “Tentar gravar”. Registros pendentes
permanecem na sessão enquanto outras partidas continuam. Uma nova vitória
também tenta gravá-los; fechar o aplicativo descarta o que ainda não foi salvo.
A gravação usa bloqueio entre instâncias e desfaz um append parcial antes
de permitir nova tentativa.

## Testes

`make test` testa somente o motor e requer apenas GLib e ferramentas de build.
Os testes incluem gravidade, colunas cheias, vitórias nas quatro direções,
alinhamentos múltiplos e longos, empate, abertura, prioridades, garfos,
armadilhas de apoio, ameaças empilhadas, acessibilidade e avaliação global,
posições espelhadas e cores trocadas, 300 partidas geradas, histórico e
recuperação de escrita parcial. A partida de 2026-10-16 às 17:32:12 é usada
como regressão: a avaliação agora prefere a coluna 4 na jogada 13 e identifica
o empilhamento que apareceu no final da sequência original. Outra regressão
cobre a derrota de 22 jogadas encontrada na abertura 7/4, com semente 0:
a coluna 2 na jogada 11 agora é identificada como perdedora por uma cadeia
forçada, e uma alternativa sem essa prova de derrota é escolhida. As duas
regressões cobrem espelhamento horizontal e troca de cores.

As vitórias do Minimax de 2026-10-16 às 18:18:02 (25 jogadas) e 18:17:50
(29 jogadas) também são regressões. A extensão com preparações identifica
como perdedoras a coluna 5 na jogada 18 e a coluna 2 na jogada 22,
respectivamente, e escolhe alternativas sem essa prova de derrota. Os casos
cobrem espelhamento e troca de cores; uma busca Minimax de profundidade 8
confirma as perdas das escolhas antigas e a existência de alternativas que
evitam essas sequências. Isso não garante empate ou vitória na partida completa.

Retomando essas duas posições antes do erro, com sementes 0–7 e ambos os
jogadores recalculando as jogadas seguintes (Minimax vermelho, profundidade 6),
a partida de 25 jogadas passou a terminar em 40 ou 41: quatro vitórias de
cada lado. A de 29 passou a terminar em 33, com oito vitórias do Minimax.
Nenhuma dessas 16 continuações terminou antes de 75% de ocupação (até 31
jogadas). Essa amostra preserva as primeiras 17 ou 21 jogadas históricas;
não representa uma taxa de vitórias para partidas iniciadas do zero.

Na comparação anterior, ao introduzir as cadeias contínuas, com sementes
0–999 para cada abertura 1/4 e 7/4, preservando
somente as duas jogadas iniciais, as 2.000 partidas entre jogadores automáticos
passaram de 1.239 vitórias de Amarelo em 22 jogadas e 761 empates para 1.239
vitórias de Amarelo em 40 jogadas e 761 empates. Não houve vitória até 31
jogadas nessa nova amostra. O resultado é condicionado às aberturas e sementes
avaliadas; não demonstra ausência de outras derrotas rápidas.

`make test-ui` compila e executa também os testes de teclado, mouse, geometria,
temporizadores, reinício, modos de jogadores e recuperação da gravação;
requer uma sessão gráfica ou um display de testes. Os testes abrem uma janela
temporária e usam históricos temporários. Para verificar visualmente, teste
janelas largas e altas, os temas claro e escuro e a marcação das vitórias.


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

Os testes reproduzem as ameaças imediatas das derrotas do Cogito de 18:43:39 e
18:43:54 em 2026-10-16: o modo assistido exige os bloqueios nas colunas 4 e 2,
respectivamente. Na derrota de 18:43:47, já existiam duas ameaças simultâneas
antes da última decisão: o filtro reconhece a ausência de defesa imediata.
Isso não garante evitar garfos preparados em turnos anteriores ou vencer o
Posicional. Os casos também cobrem espelhamento e troca de cores.

Referências do protocolo: [thinking no Ollama](https://docs.ollama.com/capabilities/thinking)
e [raciocínio ampliado do Cogito](https://ollama.com/library/cogito:3b).
