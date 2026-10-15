# Comparacao de desempenho

Tempos por operacao em milissegundos. A aceleracao divide o tempo anterior pelo posterior.

| Copias | Entidades | Etapa | Antes (ms) | Depois (ms) | Aceleracao |
| ---: | ---: | --- | ---: | ---: | ---: |
| 1 | 73 | Abrir arquivo | 13.694 | 32.562 | 0.42x |
| 1 | 73 | Duplicar desenhos | 0.004 | 0.022 | 0.17x |
| 1 | 73 | Primeiro desenho | 0.463 | 1.340 | 0.35x |
| 1 | 73 | Atualizar sem edicao | 0.082 | 0.006 | 13.21x |
| 1 | 73 | Selecionar | 0.104 | 0.016 | 6.60x |
| 1 | 73 | Editar uma peca | 0.111 | 0.012 | 8.90x |
| 1 | 73 | Snap do cursor | 0.166 | 0.031 | 5.42x |
| 1 | 73 | Snap ao mover | 1.463 | 0.041 | 35.60x |
| 1 | 73 | Localizar vetor | 0.218 | 0.040 | 5.41x |
| 1 | 73 | Pintar chapa inteira | 9.284 | 1.305 | 7.11x |
| 1 | 73 | Alterar zoom e pintar | 1.128 | 1.067 | 1.06x |
| 1 | 73 | Arrastar no viewport | 3.043 | 1.674 | 1.82x |
| 4 | 292 | Abrir arquivo | 13.894 | 15.685 | 0.89x |
| 4 | 292 | Duplicar desenhos | 0.087 | 0.101 | 0.86x |
| 4 | 292 | Primeiro desenho | 2.800 | 2.229 | 1.26x |
| 4 | 292 | Atualizar sem edicao | 0.083 | 0.022 | 3.71x |
| 4 | 292 | Selecionar | 0.092 | 0.033 | 2.84x |
| 4 | 292 | Editar uma peca | 0.088 | 0.033 | 2.66x |
| 4 | 292 | Snap do cursor | 0.702 | 0.055 | 12.66x |
| 4 | 292 | Snap ao mover | 5.588 | 0.074 | 75.28x |
| 4 | 292 | Localizar vetor | 0.084 | 0.027 | 3.06x |
| 4 | 292 | Pintar chapa inteira | 97.662 | 2.170 | 45.01x |
| 4 | 292 | Alterar zoom e pintar | 15.895 | 3.877 | 4.10x |
| 4 | 292 | Arrastar no viewport | 38.991 | 3.019 | 12.91x |
| 16 | 1168 | Abrir arquivo | 26.535 | 20.451 | 1.30x |
| 16 | 1168 | Duplicar desenhos | 0.148 | 0.125 | 1.19x |
| 16 | 1168 | Primeiro desenho | 15.519 | 7.833 | 1.98x |
| 16 | 1168 | Atualizar sem edicao | 0.347 | 0.310 | 1.12x |
| 16 | 1168 | Selecionar | 0.354 | 0.329 | 1.08x |
| 16 | 1168 | Editar uma peca | 0.477 | 0.228 | 2.10x |
| 16 | 1168 | Snap do cursor | 2.596 | 0.073 | 35.42x |
| 16 | 1168 | Snap ao mover | 10.788 | 0.432 | 24.96x |
| 16 | 1168 | Localizar vetor | 0.643 | 0.199 | 3.23x |
| 16 | 1168 | Pintar chapa inteira | 514.320 | 8.646 | 59.48x |
| 16 | 1168 | Alterar zoom e pintar | 189.890 | 18.643 | 10.19x |
| 16 | 1168 | Arrastar no viewport | 33.054 | 3.229 | 10.24x |
