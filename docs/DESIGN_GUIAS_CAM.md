# Padrão aprovado dos desenhos CAM

Aprovado pelo usuário em 14/10/2026. Novos desenhos devem manter este nível de
acabamento e esta linguagem visual; não voltar às figuras antigas ou aos ícones
esquemáticos. A prancha aprovada é a referência de direção visual, não um percurso
de máquina nem um bitmap a esticar sobre os controles.

Referência preservada: [prancha aprovada](DESIGN_GUIAS_CAM_APROVADO.png).

## Aparência

- Fundo grafite/azul-escuro e textos claros em português.
- Material claro com textura, espessura, iluminação e sombra de contato.
- Fresas metálicas com haste, canais e geometria da ponta reconhecíveis.
- Vetor nominal claro; centro da ferramenta e trajetória em ciano; pontes,
  profundidades e detalhes de seção em laranja.
- Cotas legíveis, cortes em perspectiva e detalhe ampliado quando necessário.
- Manter proporção e nitidez ao redimensionar; nunca deformar a figura.

## Comportamento

`ui/machining_guide.*` contém o modelo compartilhado e o renderizador nativo Qt.
Os dez assuntos são corte fora/dentro/sobre, profundidades, pontes, entradas/saídas,
rampas, limpeza de área, ataque rampeado, material/origem, ferramentas e estrela.

As fichas usam um rascunho local. Trocar os parâmetros atualiza todos os desenhos
do mesmo percurso, inclusive a vista ampliada. Cancelar descarta o rascunho.
O contorno selecionado é utilizado nas vistas de perfil, pontes e limpeza.
As seções de profundidade/rampa são explicações didáticas, não remoção volumétrica
exata do material selecionado.

Reprodução: animar/pausar, reiniciar, deslocar a barra de posição e escolher a
velocidade. Velocidade e sentido de reprodução não modificam avanço nem G-code.
Timers param quando a janela é ocultada; desenhos fora da região visível não
repaintam. Textura é procedural e armazenada em cache.

Na galeria, a espiral de limpeza é identificada como demonstração. O preenchimento
real do bolso continua oferecendo as estratégias já implementadas no CAM:
offset e raster. A rampa helicoidal/espiral continua ligada ao enum de rampa real.
Não confundir estes dois tipos de espiral ou prometer estratégia de corte nova
somente por haver uma animação.

## Janelas

`ui/responsive.*` limita os diálogos à área útil da tela, permite redimensionar e
oferece rolagem para telas baixas. Campos e figura ficam lado a lado quando cabem;
em janelas estreitas ficam empilhados. Grades de ferramentas quebram em linhas.
O seletor redundante “Perfil” sobre o cabeçalho do material não deve retornar.

## Verificação

`tests/test_machining_guides.cpp` verifica diferenças entre os estados, offset
interno/externo, dimensões, continuidade das rampas, modelo compartilhado, controles,
pausa/ocultação e layout estreito. `test_toolpath_dialogs.cpp` verifica os desenhos
nas nove fichas CAM. Não usar uma imagem estática oculta para satisfazer o teste.

Para testar manualmente: `scripts\run.bat --guides` ou **Exibir → Galeria interativa
de desenhos**. Em uma ficha, usar **Ampliar desenho e animação**.

`scripts\testar_guias.bat` também usa a compilação normal, não uma cópia de teste.
O atalho **CNC-CAM C++** aponta para
`%CNC_BUILD%\cnccam-RelWithDebInfo\bin\CNC-CAM.exe`: depois de alterações visuais,
compilar e conferir esse executável. Atualizar apenas `cnccam-preview` não atualiza
o aplicativo do atalho. O painel Seleção e a miniatura de material usam o mesmo
renderizador das fichas; a seleção acompanha o contorno atual sem alterar o CAM.

Se `CNC_GUIDE_CAPTURE_DIR` estiver definida, os testes gravam capturas reais do
renderizador e da ficha estreita nessa pasta. Sempre revisar as capturas junto com
os testes; passar nos testes não substitui conferir acabamento e legibilidade.
