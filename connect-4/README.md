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

Escolha 0, 1 ou 2 humanos. Com um humano, escolha vermelho ou amarelo;
vermelho sempre começa. Os discos vermelhos contêm uma cruz e os amarelos
um anel para permitir sua identificação além da cor. Quatro ou mais peças
iguais em sequência horizontal, vertical ou diagonal vencem. Todos os
alinhamentos vencedores são destacados imediatamente. Tabuleiro cheio sem
vencedor é empate.

O tabuleiro mantém proporção 7:6 e casas quadradas, centralizado na maior
área disponível abaixo dos números das colunas. O slider define a espera
antes das jogadas automáticas de 0,0 a 3,0 s, em passos de 0,1 s, inicialmente
0,5 s. Alterá-lo reinicia uma espera em andamento. O valor zero continua
cedendo o controle ao loop da interface entre jogadas.

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
O histórico registra vitória/derrota forçada e se o limite de nós foi atingido.
Não encontrar uma cadeia não demonstra segurança: jogadas de preparação sem
ameaça imediata e sequências além dos limites continuam fora dessa análise.

Uma parcela aleatória em `[0; 0,1)` é somada por coluna. O maior total vence;
em empate exato, prevalece a coluna mais à esquerda. Os pesos estão em
constantes nomeadas no motor. A avaliação posicional é complementada pela análise tática e pelas cadeias
forçadas descritas acima; não há busca geral minimax nem garantia de jogo perfeito.

## Histórico

Antes de cada jogada, inclusive humana, o histórico recebe tabuleiro anterior,
coluna e linha de queda, componentes posicionais, fatores táticos, aleatório
e total de cada coluna legal. O automático usa exatamente esses valores.
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

Na comparação com sementes 0–999 para cada abertura 1/4 e 7/4, preservando
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
