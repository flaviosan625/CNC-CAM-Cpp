# CNC-CAM C++

Porte nativo em C++20/Qt do aplicativo CNC-CAM.

## Ambiente

- C++20
- MSVC 2022
- Qt 6.12 ou superior
- CMake 3.24 ou superior
- Ninja recomendado
- GEOS
- MuPDF C++ para importação vetorial de PDF

## Compilar

Configure o projeto informando os caminhos das dependências:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_PREFIX_PATH="C:/caminho/para/Qt" `
  -DCNC_MUPDF_ROOT="C:/caminho/para/mupdf-devel"
cmake --build build --target cnccam cnc_tests
ctest --test-dir build --output-on-failure
```

O importador PDF usa a API nativa do MuPDF, sem executar Python. Ele importa
traços, preenchimentos e textos vetoriais, converte pontos para milímetros,
corrige o eixo Y e cria uma camada por página. PDFs que contenham somente
imagem raster precisam de vetorização/OCR separado.

## Importadores

- SVG: formas, polilinhas e curvas.
- DXF ASCII: LINE, CIRCLE, ARC, ELLIPSE, LWPOLYLINE, POLYLINE e SPLINE.
- PDF vetorial: todas as páginas, com camadas `PDF pN`.
- Importação em lote por pasta.

O MuPDF possui seus próprios termos de licença; verifique-os antes de
redistribuir o aplicativo.
