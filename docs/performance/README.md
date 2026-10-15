# Edicao de muitos desenhos

O benchmark e seu roteiro ficam em [tests/performance.md](../../tests/performance.md).
O projeto UNIPAR_TETO_2500MM.cnc tem 73 entidades, 6.209 pontos de polilinhas e
35 textos. As cargas de 4 e 16 copias possuem, respectivamente, 292 e 1.168
entidades no mesmo documento. O teste nao grava o arquivo de entrada.

Os desenhos do arquivo ocupam coordenadas fora da chapa, inclusive X negativo.
O arrasto usa a caixa real do desenho original para manter a aresta visivel.
As copias sao deslocadas pelo tamanho da chapa mais 25 mm; por isso alguns
desenhos se sobrepoem nessa carga densa. A pintura mostra a chapa inteira.

Compare os logs obtidos com o mesmo projeto, viewport, quantidade de operacoes
e plataforma Qt:

```powershell
.\docs\performance\compare.ps1 -Before performance-before.log -After performance-after.log -Output performance-comparison.md
```

O script recusa contagens de entidades, pontos, textos, iteracoes, viewport,
resultados de snap ou operacoes diferentes. Os tempos nao possuem limite fixo
por maquina. O teste funcional exige que o arrasto altere a geometria e que
desfazer restaure todas as entidades exatamente.

A etapa de zoom inclui aproximacao, afastamento e pintura em cada quadro,
exercitando tambem a invalidacao de caches em pixels quando a escala muda.

Na referencia anterior as otimizacoes, com Qt offscreen e 3 iteracoes, a carga
de 1.168 entidades apresentou 514,320 ms por pintura completa, 189,890 ms por
zoom com nova pintura, 10,788 ms por encaixe ao mover e 33,054 ms por quadro
do arrasto interativo de 8 quadros.
Esses numeros identificam os caminhos caros; o relatorio comparativo registra
o ganho apos a mesma carga na versao compilada com as correcoes.

Um experimento separado com a mesma geometria e cache de pixels alternou as
politicas de caneta na ordem ABBA. A caneta de largura 1,2 com cantos redondos
apresentou 337,54 e 285,83 ms por quadro de zoom; largura 1,0 com cantos em
chanfro e extremidades retas apresentou 25,23 e 23,11 ms. A media caiu de
311,68 para 24,17 ms (12,90 vezes). Isso isolou o custo da rasterizacao durante
a invalidacao do cache no zoom. O produto aplica a caneta mais rapida somente
a contornos densos nao selecionados; a geometria CAD/CAM continua integral.
