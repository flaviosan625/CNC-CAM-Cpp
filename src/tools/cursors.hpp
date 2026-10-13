// Cursores desenhados para as ferramentas (porte de src/tools/cursors.py).
//
// O cursor de tesoura do Aparar Interativo segue o Aspire: fechada por padrao e ABERTA quando esta
// sobre um vetor que da para aparar. E a unica indicacao de que o clique vai cortar ali.
#pragma once

#include <QCursor>

namespace cnc::tools::cursors {

// Tesoura fechada (padrao) ou aberta (sobre um vetor aparavel).
QCursor scissors(bool is_open = false);
// Cursor de rotacao - seta circular, para o ponto azul da alca.
QCursor rotate();
void clear_cache();

}  // namespace cnc::tools::cursors
