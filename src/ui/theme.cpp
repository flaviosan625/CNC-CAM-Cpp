// Tema do aplicativo (porte de src/ui/theme.py): superficies grafite, azul e acoes com hierarquia
// visual. A folha de estilo e o texto do Python, copiado por script; so o __ASSETS__ vira o
// prefixo dos recursos embutidos (:/assets).
#include "ui/theme.hpp"

namespace cnc::ui {

const QMap<QString, QString>& colors() {
    static const QMap<QString, QString> table = {
        {"background", "#181818"},
        {"panel", "#1f1f1f"},
        {"surface", "#252526"},
        {"border", "#2b2b2b"},
        {"text", "#cccccc"},
        {"muted", "#858585"},
        {"accent", "#0078d4"},
        {"accent_light", "#268fff"},
        {"drawing", "#e0e0e0"},
        {"canvas", "#1e1e1e"},
        {"desbaste", "#7896d2"},
    };
    return table;
}

QString color(const QString& name) { return colors().value(name); }

const QString& dark_theme() {
    static const QString sheet = QString::fromUtf8(R"QSS(
QMainWindow, QDialog { background-color: #181818; }
QWidget { color: #cccccc; font-family: 'Segoe UI', sans-serif; font-size: 12px; }
QWidget:disabled { color: #5a5a5a; }
QToolTip {
    background-color: #252526; color: #cccccc; border: 1px solid #3c3c3c;
    padding: 6px 10px; border-radius: 4px;
}
QMenuBar { background-color: #181818; border-bottom: 1px solid #2b2b2b; padding: 4px 8px; }
QMenuBar::item { padding: 5px 10px; border-radius: 4px; background: transparent; color: #cccccc; }
QMenuBar::item:selected { background-color: #2a2d2e; color: #ffffff; }
QMenu {
    background-color: #1f1f1f; border: 1px solid #2b2b2b;
    padding: 4px; border-radius: 6px;
}
QMenu::item { padding: 7px 26px 7px 16px; border-radius: 4px; color: #cccccc; }
QMenu::item:selected { background-color: #04395e; color: #ffffff; }
QMenu::separator { height: 1px; background: #2b2b2b; margin: 4px 8px; }
QMenu::item:disabled { color: #5a5a5a; }

#ToolPanel { background-color: #181818; border-right: 1px solid #2b2b2b; }
#ToolpathPanel { background-color: #181818; border-left: 1px solid #2b2b2b; }
#ToolPanelBody { background-color: #181818; }
#PropBody { background-color: #1f1f1f; }
#LeftWorkspaceSplitter::handle { background-color: #2b2b2b; }
#LeftWorkspaceSplitter::handle:hover { background-color: #0078d4; }
#WorkspaceSplitter::handle { background-color: #2b2b2b; }
#WorkspaceSplitter::handle:hover { background-color: #0078d4; }
QSplitter::handle { background-color: #2b2b2b; }
QSplitter::handle:horizontal { width: 5px; }
QSplitter::handle:vertical { height: 5px; }
QSplitter::handle:hover { background-color: #0078d4; }
QMainWindow::separator { background-color: #2b2b2b; width: 5px; height: 5px; }
QMainWindow::separator:hover { background-color: #0078d4; }
#BrandHeader {
    background-color: #181818;
    border-bottom: 1px solid #2b2b2b;
    padding: 2px 4px;
}
#BrandLogo { background: transparent; }
#BrandSubtitle { color: #858585; font-size: 10px; }
#ModeBadge {
    color: #ffffff; background-color: #0078d4; border: 1px solid #1a85ff;
    border-radius: 10px; padding: 4px 12px; font-size: 11px; font-weight: 700;
    letter-spacing: 0.5px;
}
#ModeBadge:hover { background-color: #1a85ff; border-color: #3794ff; color: #ffffff; }
#ModeBadge:pressed { background-color: #005fb8; border-color: #1a85ff; }
#ModeBadge:focus { border-color: #0078d4; }
#SectionHeader {
    color: #999999; font-weight: 700; font-size: 11px; padding: 6px 0 3px 0;
    letter-spacing: 0.8px; text-transform: uppercase;
}
#ToolTile {
    background: transparent; border: 1px solid transparent; border-radius: 6px;
}
#ToolTile:hover { background-color: #262626; border-color: #383838; }
#ToolCaption { color: #cccccc; background: transparent; border: none; font-size: 9px; }
#ToolCaption[active="true"] { color: #ffffff; font-weight: 700; }

/* Barra de camadas: seletor, cadeado e olho sob a mesma moldura. */
#LayerBar {
    background-color: #252526; border: 1px solid #2b2b2b;
    border-radius: 6px; min-height: 36px;
}
#LayerBar:hover { border-color: #0078d4; background-color: #282828; }
#LayerGlyph { background: transparent; border: none; }
#LayerCombo {
    background: transparent; border: none; border-radius: 0;
    padding: 5px 2px; color: #cccccc; font-size: 13px;
}
#LayerCombo::drop-down { border: none; width: 18px; }
#LayerToggle {
    background: transparent; border: 1px solid transparent; border-radius: 4px;
    padding: 3px;
}
#LayerToggle:hover { background-color: #313131; border-color: #3c3c3c; }
#LayerToggle:pressed { background-color: #04395e; border-color: #0078d4; }
#LayerToggle:checked { background-color: #04395e; border-color: #0078d4; }
#LayerToggle:checked:hover { background-color: #0e639c; border-color: #1a85ff; }
#LayerToggle[visibility="true"]:checked {
    background: transparent; border-color: transparent;
}
#LayerToggle[visibility="true"]:checked:hover {
    background-color: #313131; border-color: #3c3c3c;
}
#LayerToggle[visibility="true"]:!checked {
    background-color: #252526; border-color: #3c3c3c;
}
#LayerToggle:focus { border-color: #0078d4; }
#ToolBtn {
    background: transparent; border: 1px solid transparent; border-radius: 6px;
    padding: 0;
}
#ToolBtn:hover { background-color: #2a2d2e; border-color: #3c3c3c; }
#ToolBtn:pressed { background-color: #04395e; border-color: #0078d4; }
#ToolBtn:checked {
    background-color: #04395e;
    border: 1px solid #0078d4;
}
#ToolBtn:checked:hover { background-color: #0e639c; border-color: #1a85ff; }
#ToolBtn:focus { border-color: #0078d4; }
#ToolBtn:disabled { background: transparent; border-color: transparent; }
#ToolpathList {
    background-color: #252526; color: #cccccc;
    border: 1px solid #2b2b2b; border-radius: 6px;
    outline: none; padding: 4px;
}
#ToolpathList::item { padding: 5px 4px; border-radius: 4px; }
#ToolpathList::item:selected { background-color: #04395e; color: #ffffff; }
#ToolpathList::item:hover { background-color: #2a2d2e; }
#ToolpathEmpty {
    color: #858585; background: transparent; border: none;
    font-size: 12px; padding: 20px;
}
#ToolSep { background-color: #2b2b2b; border: none; max-height: 1px; }
#MaterialCard { background-color: #252526; border: 1px solid #2e2e2e; border-radius: 8px; }
#MaterialTitle { color: #75beff; font-weight: 700; font-size: 11px; background: transparent; border: none; letter-spacing: 0.5px; }
#MaterialInfo { color: #cccccc; font-size: 12px; padding: 0; background: transparent; border: none; }
#MaterialBtn {
    background-color: #313131; color: #cccccc; border: 1px solid #3c3c3c;
    border-radius: 5px; padding: 6px 12px; font-weight: 600; font-size: 11px;
}
#MaterialBtn:hover { background-color: #3c3c3c; border-color: #0078d4; color: #ffffff; }
#MaterialBtn:pressed { background-color: #04395e; }
#MaterialBtn:focus { border-color: #0078d4; }

#PropPanel { background-color: #1f1f1f; border-left: 1px solid #2b2b2b; }
#RightToolRail { background-color: #181818; border-left: 1px solid #2b2b2b; }
#RightRailButton, #RightRailToggle {
    background: transparent; border: none; border-left: 2px solid transparent; border-radius: 0; padding: 0;
    color: #858585;
}
#RightRailButton:hover, #RightRailToggle:hover {
    background-color: #222222; border-left: 2px solid transparent; color: #ffffff;
}
#RightRailButton:checked {
    background-color: #1f1f1f; border-left: 2px solid #0078d4; color: #ffffff;
}
#RightRailButton:checked:hover { background-color: #252526; color: #ffffff; }
#RightRailToggle { color: #858585; font-size: 16px; font-weight: 600; }
#RightToolRailSep { background-color: #2b2b2b; border: none; max-height: 1px; }
#PropTitle {
    background-color: #181818; color: #ffffff; font-weight: 600; font-size: 13px;
    padding: 12px 16px; border-bottom: 1px solid #2b2b2b;
}
#PropHint { color: #858585; font-size: 11px; padding: 10px 16px; background-color: #1f1f1f; }
#PropLabel { color: #858585; font-size: 11px; padding-top: 4px; padding-bottom: 2px; }
#ApplyBtn, QPushButton[primary="true"], QPushButton:default {
    background-color: #0078d4; color: #ffffff; border: 1px solid #0078d4;
    border-radius: 5px; padding: 8px 14px; font-weight: 600;
}
#ApplyBtn:hover, QPushButton[primary="true"]:hover, QPushButton:default:hover {
    background-color: #1a85ff; border-color: #1a85ff;
}
#ApplyBtn:pressed, QPushButton[primary="true"]:pressed, QPushButton:default:pressed { background-color: #005fb8; }
#ApplyBtn:disabled, QPushButton[primary="true"]:disabled { background-color: #252526; color: #5a5a5a; border-color: #2b2b2b; }
#SimulatePathBtn {
    background-color: #0e639c; color: #ffffff; border: 1px solid #1177bb;
    border-radius: 5px; padding: 8px 13px; font-weight: 600;
}
#SimulatePathBtn:hover { background-color: #1177bb; border-color: #3794ff; }
#SimulatePathBtn:pressed { background-color: #0b4f7a; }
#SimulationProgress {
    min-height: 18px; max-height: 18px;
    background-color: #252526; border: 1px solid #2b2b2b; border-radius: 5px;
    text-align: center; color: #ffffff;
}
#SimulationProgress::chunk {
    background-color: #0078d4; border-radius: 4px;
}
#AnchorDot::indicator { width: 14px; height: 14px; }

QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
    background-color: #252526; border: 1px solid #3c3c3c; border-radius: 5px;
    padding: 6px 8px; color: #cccccc; selection-background-color: #04395e; min-height: 18px;
}
QComboBox:hover, QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: #555555; }
QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #0078d4; background-color: #2a2a2a; }
QComboBox:disabled, QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
    color: #5a5a5a; border-color: #2b2b2b; background-color: #1f1f1f;
}
QComboBox { padding-right: 25px; }
QComboBox::drop-down { border: none; width: 25px; }
QComboBox::down-arrow { image: url(__ASSETS__/chevron-down.svg); width: 12px; height: 12px; }
QComboBox QAbstractItemView {
    background-color: #1f1f1f; color: #cccccc; selection-background-color: #04395e;
    border: 1px solid #2b2b2b; padding: 4px; outline: none;
}
QSpinBox::up-button, QDoubleSpinBox::up-button {
    subcontrol-origin: border; subcontrol-position: top right; width: 22px;
    border: none; background: transparent;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border; subcontrol-position: bottom right; width: 22px;
    border: none; background: transparent;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background-color: #2a2d2e; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(__ASSETS__/chevron-up.svg); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(__ASSETS__/chevron-down.svg); width: 10px; height: 10px; }
QCheckBox, QRadioButton { color: #cccccc; spacing: 8px; padding: 3px 0; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 15px; height: 15px; background-color: #252526; border: 1px solid #3c3c3c; border-radius: 4px;
}
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: #0078d4; }
QCheckBox::indicator:checked { background-color: #0078d4; border-color: #0078d4; image: url(__ASSETS__/check.svg); }
QRadioButton::indicator { border-radius: 8px; }
QRadioButton::indicator:checked { background-color: #0078d4; border-color: #0078d4; image: url(__ASSETS__/radio-dot.svg); }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { background-color: #1f1f1f; border-color: #2b2b2b; }
QGroupBox {
    background-color: #252526; border: 1px solid #2b2b2b; border-radius: 6px;
    margin-top: 14px; padding-top: 12px; font-weight: 600;
}
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #75beff; }
#CamConfigGroup { margin-top: 0; padding-top: 0; }
#CamConfigGroup::title { subcontrol-origin: margin; font-size: 0px; color: transparent; width: 0px; height: 0px; margin: 0px; padding: 0px; border: none; }
#CamConfigHeader {
    background-color: #181818; border: none; border-bottom: 2px solid #0078d4;
    border-top-left-radius: 6px; border-top-right-radius: 6px;
}
#CamConfigHeaderTitle { color: #e0e0e0; font-weight: 600; }
#GroupCollapseBtn {
    background: transparent; color: #75beff; border: 1px solid #3c3c3c;
    border-radius: 4px; padding: 2px 8px; font-size: 11px; font-weight: 500;
}
#GroupCollapseBtn:hover { background-color: #2a2d2e; color: #ffffff; border-color: #0078d4; }
#GroupCollapseBtn:pressed { background-color: #04395e; }
#SectionNavPill {
    background-color: #252526; color: #858585; border: 1px solid #2b2b2b;
    border-radius: 12px; padding: 3px 10px; font-size: 11px; font-weight: 500;
}
#SectionNavPill:hover { background-color: #2a2d2e; color: #ffffff; border-color: #0078d4; }
#SectionNavPill:pressed { background-color: #04395e; }
QPushButton {
    background-color: #313131; border: 1px solid #3c3c3c; border-radius: 5px;
    padding: 7px 14px; color: #cccccc; font-weight: 500;
}
QPushButton:hover { background-color: #3c3c3c; border-color: #555555; color: #ffffff; }
QPushButton:pressed { background-color: #282828; }
QPushButton:focus { border-color: #0078d4; }
QPushButton:disabled { color: #5a5a5a; background-color: #252526; border-color: #2b2b2b; }

#ViewTabs { background-color: #181818; border-bottom: 1px solid #2b2b2b; }
#ViewTabs::tab {
    background-color: #181818; color: #969696; padding: 9px 22px;
    border: none; border-bottom: 2px solid transparent; min-width: 105px; font-weight: 600; font-size: 12px;
}
#ViewTabs::tab:selected { color: #ffffff; background-color: #1f1f1f; border-bottom: 2px solid #0078d4; font-weight: 700; }
#ViewTabs::tab:hover:!selected { color: #ffffff; background-color: #222222; }
#View3DControls { background-color: #181818; border: none; }
#View3DBtn {
    background-color: #252526; color: #cccccc; border: 1px solid #3c3c3c;
    border-radius: 4px; padding: 0 7px; font-size: 10px; font-weight: 600;
}
#View3DBtn:hover { background-color: #2a2d2e; color: #ffffff; border-color: #0078d4; }
#View3DBtn:pressed { background-color: #04395e; border-color: #0078d4; }
#BottomTabs { background-color: #181818; border-top: 1px solid #2b2b2b; }
#BottomTabs::tab { color: #969696; padding: 10px 0; border: none; border-top: 2px solid transparent; font-weight: 600; }
#BottomTabs::tab:selected { color: #ffffff; background-color: #1f1f1f; border-top: 2px solid #0078d4; }
#BottomTabs::tab:hover:!selected { background-color: #222222; color: #e0e0e0; }
#ToolPanel[compact="true"] #BottomTabs::tab { padding: 4px 0; font-size: 11px; }
#ToolPanel[compact="true"] #MaterialBtn { padding: 4px 8px; }
#ToolPanel[compact="true"] QComboBox { padding-top: 4px; padding-bottom: 4px; }

QStatusBar { background-color: #181818; color: #858585; font-size: 11px; border-top: 1px solid #2b2b2b; padding: 2px 10px; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #858585; padding: 0 8px; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: #37373d; border-radius: 5px; min-height: 30px; margin: 1px; }
QScrollBar::handle:vertical:hover { background: #4e4e56; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: #37373d; border-radius: 5px; min-width: 30px; margin: 1px; }
QScrollBar::handle:horizontal:hover { background: #4e4e56; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }
)QSS").replace("__ASSETS__", ":/assets");
    return sheet;
}

}  // namespace cnc::ui
