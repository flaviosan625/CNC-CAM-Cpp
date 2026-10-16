# Desempenho ao arrastar grupos

Medições feitas em 16/10/2026 no Windows, viewport 1280 × 800, usando o projeto `UNIPAR_TETO_2500MM.cnc`.

## Resultado no projeto original

O projeto tem 73 entidades, 6.209 pontos de polilinha e 35 textos. O arrasto do grupo caiu de aproximadamente 1.228 ms/quadro para 16,5 ms/quadro. O teste também confirma que o movimento fica correto no canvas e que desfazer restaura a geometria.

## Arquivo de estresse

Foi gerado localmente `D:\CORTES ROUTER\UNIPAR\UNIPAR_TETO_2500MM_2336_ENTIDADES_STRESS.cnc`, com 2.336 entidades, 198.688 pontos de polilinha e 1.120 textos (15,75 MB). Ele contém 32 cópias do desenho organizadas em uma chapa maior. O benchmark carregou o arquivo e passou as verificações de movimento e desfazer.

Com as 2.336 entidades selecionadas, as medições foram:

| Operação | Tempo |
| --- | ---: |
| Abrir o arquivo | 922 ms |
| Renderizar a chapa inteira | 387 ms/quadro |
| Recalcular o zoom | 160 ms/quadro |
| Renderizar o grupo selecionado | 4.833 ms/quadro |
| Arrastar o grupo | 4.419 ms/quadro |
| Confirmar o movimento ao soltar | 3.814 ms |

## Próximo gargalo

O arrasto por pré-visualização deixa o projeto original fluido, mas a renderização dos caminhos vetoriais selecionados volta a dominar quando milhares de entidades se movem juntas. O mouse-up também tem custo alto ao criar o comando de desfazer e atualizar a cena. O próximo trabalho deve medir e otimizar a prévia de grupos grandes e o commit em lote, mantendo snap e desfazer corretos.

Para gerar outro arquivo maior com o benchmark, defina `CNC_PERF_EXPORT_PATH`, `CNC_PERF_COPIES` e `CNC_PERF_PROJECT` antes de executar o caso `performance: projeto real*`. A exportação é opcional; por padrão, o teste não grava arquivos.
