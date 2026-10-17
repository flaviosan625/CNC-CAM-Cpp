# Auditoria das ferramentas 2D

Esta lista descreve o que os 54 comandos visíveis do painel fazem no código C++ atual. É uma auditoria de ligação e implementação, não uma afirmação de que cada combinação de entrada foi validada manualmente. As rotas estão em `MainWindow::create_tool_panel()` e `MainWindow::activate_tool()`; os comportamentos estão em `src/tools` e `src/viewport`.

## Arquivo, vista e inspeção

| Comando | Efeito atual |
| --- | --- |
| Importar | Abre seletor e envia os arquivos aceitos ao importador. |
| Exportar | Exporta o desenho em SVG. |
| Galeria de guias | Abre a galeria de desenhos interativos. |
| Prévia 3D | Muda para a aba 3D. |
| Medir | Dois cliques mostram distância, ΔX, ΔY e ângulo; não cria vetor. |
| Grade | Alterna a grade da vista 2D. |
| Material/área | Abre a configuração de material e chapa. |
| Mão | Arrasta a vista; botão do meio e espaço também permitem deslocá-la. |
| Seleção | Seleciona, movimenta e transforma por alças; respeita camadas ocultas/travadas. |

## Desenho

| Comando | Efeito atual |
| --- | --- |
| Círculo | Arraste do centro até o raio. |
| Elipse | Arraste da caixa; Shift/Ctrl igualam os raios; Alt usa o primeiro ponto como centro. |
| Retângulo | Arraste da caixa; Shift/Ctrl cria quadrado; Alt desenha a partir do centro. |
| Polígono | Centro e raio por arraste, com número de lados configurado. |
| Estrela | Centro e raio por arraste, com pontas e proporção do raio interno. |
| Polilinha | Cliques acrescentam nós; Enter/duplo clique terminam; C/Tab fecha; Shift trava o segmento na horizontal/vertical. |
| Texto | Clique posiciona o texto e usa conteúdo, fonte e altura configurados. |
| Linha | Dois cliques criam um segmento. |
| Arco | Três cliques definem o arco. |
| Linha de construção | Dois cliques criam uma guia separada dos vetores de corte. |
| Desenho livre | Arraste registra uma sequência de pontos. |
| Ponto de furação | Clique acrescenta um ponto de furação. |
| Textura vetorial | Recorta linhas paralelas ou cruzadas dentro dos vetores fechados selecionados. |
| Texto em curva | Usa uma curva previamente selecionada como guia e cria contornos do texto sobre ela. |
| Cota | Cria cotas lineares, alinhadas, horizontais, verticais, angulares, de raio ou diâmetro conforme o modo escolhido. |
| Vetorizar bitmap | Abre uma imagem, reduz a até 400 pixels no maior lado e extrai contornos por limiar em tons de cinza. |

## Transformação

| Comando | Efeito atual |
| --- | --- |
| Mover | Arrasta a seleção; também há aplicação de deslocamento por parâmetros e opção de cópia. |
| Distorcer | Arrasta um dos quatro cantos da caixa da seleção. |
| Espelhar | Espelha horizontal ou verticalmente em torno da seleção, com opção de cópia. |
| Redimensionar | Escala por arraste ou parâmetros, com opção proporcional e de cópia. |
| Cópia em array | Replica a seleção em linhas e colunas usando passo ou folga. |
| Cópia circular | Replica ao redor de um centro por quantidade e ângulo. |
| Rotacionar | Gira em torno do centro; Alt trava passos de 15°. |

## Edição vetorial

| Comando | Efeito atual |
| --- | --- |
| Editar nós | Move nós; duplo clique insere; Delete/Backspace/D remove quando resta o mínimo permitido. |
| Filete | Arredonda o canto encontrado próximo ao clique. |
| Aparar objetos | Usa o último vetor fechado selecionado como limite e mantém dentro ou fora conforme a configuração. |
| Aparar interativo | Clica no trecho a aparar; o cursor mostra quando encontra um alvo. |
| Estender | Estende um vetor até um limite próximo. |
| Ajustar curvas | Simplifica a geometria selecionada pela tolerância; pode manter os originais. |
| Unir vetores | Junta extremidades de vetores abertos dentro da tolerância configurada. |
| Soldar | União geométrica de vetores fechados selecionados. |
| Subtrair | Usa o último vetor fechado selecionado como subtrator. |
| Interseção | Mantém a área comum dos vetores fechados selecionados. |
| Agrupar | Atribui um mesmo grupo aos objetos selecionados. |
| Desagrupar | Remove a associação de grupo. |
| Offset | Cria contornos paralelos pela distância, direção e tipo de junção. |
| Contorno vetorial | Cria uma borda ao redor da seleção com offset configurável. |
| Converter texto em curvas | Troca o texto selecionado por polilinhas de seus contornos. |

## Alinhamento e organização

| Comando | Efeito atual |
| --- | --- |
| Esquerda, centro horizontal, direita | Alinha a seleção ao último objeto selecionado; se só há um, usa a chapa. |
| Topo, centro vertical, base | Mesma regra de referência, no eixo vertical. |
| Organizar material | Calcula posições para encaixar os objetos selecionados na chapa. |

## Regras compartilhadas e pontos encontrados

- `Ctrl+C`, `Ctrl+X` e `Ctrl+V` operam nos vetores editáveis selecionados quando a janela está ativa, inclusive com foco em outro controle do painel. Campos de texto mantêm seus atalhos nativos. A cópia interna preserva camada e cria novos IDs; a colagem desloca 10 mm em X e -10 mm em Y, seleciona os novos objetos e entra no desfazer.
- A área de transferência de vetores é interna a esta janela; copiar vetores para outro aplicativo ou outra janela do programa ainda não faz parte desta implementação.
- A tecla `C` sem Ctrl continua fechando a polilinha. `Ctrl+C` passa antes do tratamento de letras pela ferramenta.
- O menu contextual oferecia “Separar bloco de texto em linhas”, mas faltava registrar `SplitTextLinesTool`; a ligação foi corrigida e coberta por teste.
- O zoom da roda agora usa a posição do evento no canvas como âncora. Isso impede o salto causado pela posição global do cursor.
- Em vista muito afastada, texto menor que 5 pixels aparece como um quadro leve; ao aproximar, o contorno integral reaparece. A geometria do documento e os percursos não são alterados.

## Validação e limite

- Os 54 nomes do painel foram comparados às ações diretas e ao registro de ferramentas; nenhum ficou sem rota após a correção do texto em linhas.
- Testes de teclado cobrem copiar, colar, recortar, desfazer, foco em campo de texto e a ferramenta de separar linhas. O teste de zoom usa o arquivo `UNIPAR_TETO_2500MM.cnc` e 1, 4, 16 e 64 cópias em memória.
- A bateria sem os casos `golden:*` passou: 102 casos e 8.559 verificações. O conjunto completo ainda contém oito casos `golden:*` com divergências de referências antigas de geometria/CAM; essas referências precisam de avaliação própria antes de ser atualizadas.

## Medição no arquivo UNIPAR

No Windows, com 64 cópias em memória do arquivo `UNIPAR_TETO_2500MM.cnc` (4.672 entidades, 397.376 pontos de polilinha e 2.240 textos), o teste isolou o custo dos textos em vista afastada:

| Medida | Antes | Depois |
| --- | ---: | ---: |
| Renderização apenas dos textos | 279,5 ms | 23,7 ms |
| Renderização da chapa inteira | 498,6 ms | 107,7 ms |
| Quadro final após usar a roda | 1.999,9 ms | 99–172 ms em três rodadas |

Os passos de roda ficaram em torno de 9–16 ms nesse cenário. São medições locais, dependentes de hardware e da posição do zoom. O teste confirma que a geometria original permanece e que a prévia do zoom mostra o desenho.

A escolha de substituir texto ilegível por uma caixa segue o recurso [QTEXT documentado pela Autodesk](https://help.autodesk.com/cloudhelp/2025/ENG/AutoCAD-Core/files/GUID-DFBE6C48-893B-4CE1-9B28-0656FABFAAB7.htm). O programa já limita o destaque de seleções grandes e mantém uma prévia rasterizada durante a navegação, ideias relacionadas ao [limite de prévia de seleção](https://help.autodesk.com/cloudhelp/2020/ENU/AutoCAD-Core/files/GUID-9CDDA4F9-5B88-428F-B0C8-345E9A9DC4F1.htm) e às [opções de desempenho gráfico 2D](https://help.autodesk.com/cloudhelp/2025/ENU/AutoCAD-Core/files/GUID-14929CF2-8E39-4F87-B8C2-18440B12922E.htm) do AutoCAD.
