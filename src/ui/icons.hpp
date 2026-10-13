// Icones desenhados com QPainter (porte do gerador de icones de src/ui/tool_panel.py).
#pragma once

#include <QIcon>
#include <QString>

namespace cnc::ui {

// Icone pelo nome ("circle", "zoom_fit", "lock", ...). Nome desconhecido devolve um icone vazio.
QIcon get_icon(const QString& name);

}  // namespace cnc::ui
