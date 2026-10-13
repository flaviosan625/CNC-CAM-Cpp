// Tema do aplicativo (porte de src/ui/theme.py).
#pragma once

#include <QMap>
#include <QString>

namespace cnc::ui {

// Cores nomeadas: background, panel, surface, border, text, muted, accent, accent_light, drawing, canvas.
const QMap<QString, QString>& colors();
QString color(const QString& name);

// Folha de estilo escura completa (QSS) que o QApplication/MainWindow aplica.
const QString& dark_theme();

}  // namespace cnc::ui
