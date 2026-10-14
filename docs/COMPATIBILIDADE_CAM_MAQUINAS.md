# Compatibilidade CAM, CAD e controladoras

Este mapa separa três coisas que costumam ser confundidas: o CAD que fornece os vetores, o CAM que calcula o percurso e a controladora que interpreta o G-code. “Compatível com a marca” não é uma garantia suficiente: o modelo, firmware, macros e configuração da máquina definem o pós-processador correto.

## Estado atual no CNC-CAM C++

| Camada | Situação | Observação prática |
|---|---|---|
| Entrada de vetores | PDF, DXF e SVG | DWG não é um formato de entrada direto do importador atual; converter/exportar para DXF ou SVG no CAD. |
| Aspire/Vectric | Pontes e parâmetros CAM mapeados | Distribuição por quantidade ou espaçamento, posição manual/arraste, 2D/3D e folha de serviço. |
| Exportação G-code | GRBL, Mach3 e LinuxCNC | Escolha de pós-processador agora aparece antes de salvar. Numeração, comentários, retorno ao ponto inicial e troca M6 são configuráveis. |
| GRBL | Perfil conservador | M6 fica desligado por padrão; o firmware GRBL v1.1 lista os comandos aceitos e não inclui M6 nessa lista. |
| Mach3 | Perfil genérico | M6 depende das macros e do método de troca definidos na instalação. Conferir o manual e o perfil da máquina. |
| LinuxCNC | Perfil genérico | Confirmar G54, origem da peça, configuração de ferramenta e comportamento de M6. |
| RichAuto/DSP, DDCS, NCStudio, Syntec, Fanuc, Siemens e Mach4 | Ainda sem pós dedicado validado | Requer manual do modelo/firmware, arquivo de amostra e teste de simulação antes de habilitar como perfil próprio. |

O perfil Mach3 não deve ser tomado como perfil Mach4, mesmo que ambos aceitem parte do mesmo G-code. Da mesma forma, um controle RichAuto ou de outro fabricante não deve ser classificado apenas como “ISO” sem confirmar seus limites e convenções.

## Mapeamento recomendado por software

| Origem | Fluxo preferencial | Conferências antes de gerar percurso |
|---|---|---|
| SOLIDWORKS | Desenho 2D → DXF → CNC-CAM | Salvar mapa de camadas para separar corte, gravação, furação e cotas; conferir unidade e escala no DXF. |
| Aspire | DXF/SVG/PDF → CNC-CAM, ou usar a folha de configuração do Aspire como referência | Conferir dimensões do trabalho, origem XY/Z, espessura, ferramenta, velocidades e pós selecionado. |
| Adobe Illustrator | DXF/SVG → CNC-CAM | Na importação DXF/DWG selecionar tamanho original e unidade correta; não mesclar camadas quando elas codificam operações diferentes. |
| Outros CAD/CAM | Exportar DXF ou SVG para o desenho; obter manual/post do controle para a saída | Não aceitar escala inferida, origem presumida ou troca de ferramenta sem validação. |

## Dados obrigatórios para habilitar um novo fabricante/modelo

1. Fabricante e modelo exatos da máquina e da controladora.
2. Versão do firmware e versão do software que envia o programa.
3. Manual oficial do modelo e, se houver, arquivo `.pp`/post oficial do fabricante do CAM.
4. Um programa G-code conhecido que já rode nessa máquina, incluindo cabeçalho, troca de ferramenta, spindle, fim e retorno.
5. Unidade, plano, modo absoluto, origem de trabalho (ex.: G54), zero Z, limites de curso e altura segura.
6. Suporte a M6/ATC, M3/M4/M5, refrigeração, arcos, subprogramas e comentários.
7. Simulação do percurso, comparação visual com o Aspire e teste a seco acima do material antes de um corte real.

Sem esses dados, o programa pode exibir a família como “mapeada para análise”, mas não deve apresentar um pós dedicado como validado.

## Fontes oficiais consultadas

- [Vectric Aspire — Toolpath Tabs](https://docs.vectric.com/docs/V12.5/Aspire/ENU/Help/form/Toolpath%20Tabs/index.html): distribuição por quantidade/espaçamento, marcação por clique, remoção e arraste das pontes.
- [Vectric Aspire — Job Setup Sheet Editor](https://docs.vectric.com/docs/V12.5/Aspire/ENU/Help/form/job-setup-sheet-editor/index.html): folha de configuração, logotipo e resumo de material e percursos.
- [Vectric — Post Processor Management](https://docs.vectric.com/docs/V12.5/VCarveDesktop/ENU/Help/form/post-processor-management/index.html): a controladora exige o pós adequado; pós podem ser configurados para a máquina.
- [SOLIDWORKS — DXF/DWG File Mapping](https://help.solidworks.com/2024/english/SolidWorks/sldworks/t_DXF_DWG_File_Mapping.htm?id=18.16.10.6): entidades e cores podem ser mapeadas para camadas e o mapa salvo/reutilizado.
- [Adobe Illustrator — Import AutoCAD files](https://helpx.adobe.com/illustrator/desktop/add-and-import-files/import-other-file-types/import-autocad-files.html): escala, unidade, layout e opção de mesclar camadas no DXF/DWG.
- [GRBL v1.1 — Supported G-codes](https://github.com/gnea/grbl): referência do projeto GRBL upstream para os comandos aceitos.
- [Mach Support — Product Manuals](https://www.machsupport.com/help-learning/product-manuals/) e [Mach3Mill Install/Config manual](https://www.machsupport.com/ftp/Docs/Mach3Mill_Install_Config.pdf): instalação, configuração e macros do Mach3.
- [LinuxCNC — Coordinate Systems](https://linuxcnc.org/docs/stable/html/gcode/coordinates.html): G53, offsets G54–G59.3 e sua interação com a origem da peça.
- [RichAuto — Downloads](https://www.richautocontroller.com/download/), [DSP A11 Manual](https://www.richautocontroller.com/wp-content/uploads/2023/08/DSP-A11-Manual.pdf) e [A1X A11/A12/A15/A18 Manual](https://www.richautocontroller.com/wp-content/uploads/2023/12/A1X_Motion_Control_SystemA11%E3%80%81A12%E3%80%81A15%E3%80%81A18%E2%80%94Manual.pdf): documentação de famílias DSP; confirmar a revisão instalada.
- [Autodesk Fusion — Post Processor Customization](https://help.autodesk.com/cloudhelp/ENU/Fusion-CAM/files/Post-processor-customization.htm): referência de por que o modelo, a controladora e um programa de exemplo precisam ser conhecidos para customizar um pós.
