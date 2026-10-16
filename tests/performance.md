# Desempenho ao editar muitos desenhos

O teste `performance: projeto real com muitas pecas no mesmo documento` e opt-in.
Ele abre uma copia em memoria do arquivo fornecido, duplica todos os desenhos em
grade no mesmo documento e amplia a chapa. Mantem textos, polilinhas detalhadas
e grupos independentes. O projeto original nao e gravado.

O teste `performance: roda no arquivo real antes de calcular percursos` usa o
mesmo arquivo e envia eventos reais de roda ao viewport nativo. Ele mede o
tempo dos dois primeiros passos (`wheel_ms`), o quadro pronto (`resting_ms`)
e uma espera de 170 ms que processa o retorno da pintura (`settle_ms`). Tambem verifica
que oito eventos de 15 unidades equivalem a um entalhe de 120 unidades, que
a roda horizontal nao altera o zoom e que o desenho continua visivel durante
a pre-visualizacao. Para avaliar uma carga maior:

```powershell
$env:CNC_PERF_RUN = '1'
$env:CNC_PERF_COPIES = '1,16,64,256'
$env:QT_QPA_PLATFORM = 'windows'
.\cnc_tests.exe '--test-case=performance: roda no arquivo real antes de calcular percursos'
```

Exemplo em PowerShell, executando a partir da pasta do executavel de testes:

```powershell
$env:CNC_PERF_RUN = '1'
$env:CNC_PERF_PROJECT = 'D:\CORTES ROUTER\UNIPAR\UNIPAR_TETO_2500MM.cnc'
$env:CNC_PERF_COPIES = '1,4,16'
$env:CNC_PERF_ITERATIONS = '3'
$env:QT_QPA_PLATFORM = 'offscreen'
.\cnc_tests.exe '--test-case=performance: projeto real*' 2>&1 | Tee-Object performance-after.log
```

Repita com o executavel anterior para gerar `performance-before.log`, mantendo
arquivo, variaveis, viewport, configuracao otimizada e plataforma Qt identicos.
O ponto de entrada dos testes isola a configuracao do usuario em pasta temporaria.

Cada linha `CNC_PERF` contem JSON com contagens e milissegundos por etapa:

- Carregamento e duplicacao do arquivo, primeiro desenho e atualizacoes sem edicao.
- Mudanca de selecao e edicao repetida de uma unica peca.
- Snap do cursor, encaixe ao mover e busca do vetor sob o cursor.
- Pintura da chapa inteira, zoom com nova pintura e arrasto com 8 eventos reais no viewport.

As consultas de snap usam 2 px/mm, independente do numero de copias. A pintura
mostra a chapa inteira. O arrasto mostra o bloco original no mesmo zoom e envia
press/move/release pelo viewport, incluindo repintura entre os quadros. O teste
exige que o arrasto crie um comando de desfazer e que desfazer restaure exatamente
as entidades. Isso evita medir uma interacao vazia.

A etapa `zoom_repaint` alterna aproximacao de 15% e retorno, com nova pintura
em imagem em cada passo. Sao 3 pares por iteracao. Ela inclui o custo de
invalidar caches de pixels ao mudar o zoom, alem de pintar a geometria.

Os resultados nao tem limite fixo de tempo para evitar falhas dependentes do
hardware. Compare `ms_per_operation` e o crescimento com o numero de pecas.
O teste de cache independente roda sempre e verifica que selecionar nao refaz
a geometria e que editar invalida somente a peca alterada.

Para acompanhar visualmente o arrasto, use `QT_QPA_PLATFORM=windows`. Mantenha
essa mesma plataforma nos dois executaveis ao comparar numeros.
