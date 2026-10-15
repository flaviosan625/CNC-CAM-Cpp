// Tema do aplicativo (porte de src/ui/theme.py): superficies grafite, azul e acoes com hierarquia
// visual. A folha de estilo e o texto do Python, copiado por script; so o __ASSETS__ vira o
// prefixo dos recursos embutidos (:/assets).
#include "ui/theme.hpp"

namespace cnc::ui {

const QMap<QString, QString>& colors() {
    static const QMap<QString, QString> table = {
        {"background", "#141b25"},
        {"panel", "#192230"},
        {"surface", "#202c3d"},
        {"border", "#2b3b50"},
        {"text", "#e6edf6"},
        {"muted", "#96a9c1"},
        {"accent", "#2589f5"},
        {"accent_light", "#77bcff"},
        {"drawing", "#263746"},
        {"canvas", "#d9dde2"},
    };
    return table;
}

QString color(const QString& name) { return colors().value(name); }

const QString& dark_theme() {
    static const QString sheet = QString::fromUtf8(R"QSS(
QMainWindow, QDialog { background-color: #141b25; }
QWidget { color: #e6edf6; font-family: 'Segoe UI', sans-serif; font-size: 12px; }
QWidget:disabled { color: #62758f; }
QToolTip {
    background-color: #24344a; color: #edf5ff; border: 1px solid #405a78;
    padding: 7px 10px; border-radius: 6px;
}
QMenuBar { background-color: #141b25; border-bottom: 1px solid #2b3b50; padding: 5px 10px; }
QMenuBar::item { padding: 6px 12px; border-radius: 6px; background: transparent; color: #aebfd4; }
QMenuBar::item:selected { background-color: #243851; color: #f2f7ff; }
QMenu {
    background-color: #1b2737; border: 1px solid #354a63;
    padding: 6px; border-radius: 8px;
}
QMenu::item { padding: 8px 30px 8px 18px; border-radius: 5px; }
QMenu::item:selected { background-color: #244f7b; }
QMenu::separator { height: 1px; background: #304157; margin: 5px 10px; }
QMenu::item:disabled { color: #62758f; }

#ToolPanel { background-color: #182230; border-right: 1px solid #344a62; }
#ToolpathPanel { background-color: #182230; border-left: 1px solid #344a62; }
#ToolPanelBody { background-color: #182230; }
#PropBody { background-color: #192230; }
#LeftWorkspaceSplitter::handle { background-color: #263342; }
#LeftWorkspaceSplitter::handle:hover { background-color: #4a8dcc; }
#WorkspaceSplitter::handle { background-color: #263342; }
#WorkspaceSplitter::handle:hover { background-color: #4a8dcc; }
#BrandHeader {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                stop:0 #1c2a3b, stop:1 #17212f);
    border-bottom: 1px solid #38536f;
}
#BrandLogo { background: transparent; }
#BrandSubtitle { color: #8199b6; font-size: 10px; }
#ModeBadge {
    color: #a4d4ff; background-color: #22364c; border: 1px solid #47749d;
    border-radius: 10px; padding: 7px 16px; font-size: 13px; font-weight: 600;
    letter-spacing: 0.6px;
}
#ModeBadge:hover { background-color: #2a4a68; border-color: #82bbed; color: #ffffff; }
#ModeBadge:pressed { background-color: #25669c; border-color: #a3d6ff; }
#ModeBadge:focus { border-color: #b2ddff; }
#SectionHeader {
    color: #b9d0e8; font-weight: 500; font-size: 14px; padding: 2px 0;
    letter-spacing: 0.6px;
}
#ToolTile {
    background: transparent; border: 1px solid transparent; border-radius: 10px;
}
#ToolTile:hover { background-color: #22374c; border-color: #3d5c7c; }
#ToolCaption { color: #d5e4f3; background: transparent; border: none; }
#ToolCaption[active="true"] { color: #ffffff; font-weight: 600; }

/* Barra de camadas: seletor, cadeado e olho sob a mesma moldura. */
#LayerBar {
    background-color: #1b2a3b; border: 1px solid #3b5875;
    border-radius: 10px; min-height: 39px;
}
#LayerBar:hover { border-color: #669bc9; background-color: #20344a; }
#LayerGlyph { background: transparent; border: none; }
#LayerCombo {
    background: transparent; border: none; border-radius: 0;
    padding: 5px 2px; color: #eaf4ff; font-size: 13px;
}
#LayerCombo::drop-down { border: none; width: 18px; }
#LayerToggle {
    background: transparent; border: 1px solid transparent; border-radius: 6px;
    padding: 3px;
}
#LayerToggle:hover { background-color: #2e4c68; border-color: #6399ca; }
#LayerToggle:pressed { background-color: #235d91; border-color: #85c3f5; }
#LayerToggle:checked { background-color: #264a6b; border-color: #4075a2; }
#LayerToggle:checked:hover { background-color: #35618a; border-color: #7ab6e9; }
#LayerToggle[visibility="true"]:checked {
    background: transparent; border-color: transparent;
}
#LayerToggle[visibility="true"]:checked:hover {
    background-color: #2e4c68; border-color: #6399ca;
}
#LayerToggle[visibility="true"]:!checked {
    background-color: #263b51; border-color: #496784;
}
#LayerToggle:focus { border-color: #a5d7ff; }
#ToolBtn {
    background: transparent; border: 1px solid transparent; border-radius: 10px;
    padding: 0;
}
#ToolBtn:hover { background-color: #2b4764; border-color: #6195c6; }
#ToolBtn:pressed { background-color: #1e629f; border-color: #95cbff; }
#ToolBtn:checked {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                stop:0 #386f9f, stop:1 #254565);
    border: 1px solid #74b4ed;
}
#ToolBtn:checked:hover { background-color: #3479b5; border-color: #a5d9ff; }
#ToolBtn:focus { border-color: #b2ddff; }
#ToolBtn:disabled { background: transparent; border-color: transparent; }
#ToolpathList {
    background-color: #1b2a3b; color: #d5e4f3;
    border: 1px solid #3b5875; border-radius: 9px;
    outline: none; padding: 5px;
}
#ToolpathList::item { padding: 6px 5px; border-radius: 5px; }
#ToolpathList::item:selected { background-color: #2b5d89; color: #ffffff; }
#ToolpathList::item:hover { background-color: #29435e; }
#ToolpathEmpty {
    color: #a9bfd6; background: transparent; border: none;
    font-size: 12px; padding: 20px;
}
#ToolSep { background-color: #3a536d; border: none; max-height: 1px; }
#MaterialCard { background-color: #202f43; border: 1px solid #304a67; border-radius: 10px; }
#MaterialTitle { color: #82bfff; font-weight: 600; font-size: 11px; background: transparent; border: none; }
#MaterialInfo { color: #e0ecfa; font-size: 12px; padding: 0; background: transparent; border: none; }
#MaterialBtn {
    background-color: #284564; color: #d9edff; border: 1px solid #3b6590;
    border-radius: 7px; padding: 7px 10px; font-weight: 600;
}
#MaterialBtn:hover { background-color: #315b87; border-color: #66acfa; }
#MaterialBtn:pressed { background-color: #236fc0; }
#MaterialBtn:focus { border-color: #91ccff; }

#PropPanel { background-color: #192230; border-left: 1px solid #2b3b50; }
#RightToolRail { background-color: #1c222b; border-left: 1px solid #343b45; }
#RightRailButton, #RightRailToggle {
    background: transparent; border: 1px solid transparent; border-radius: 7px; padding: 0;
    color: #c5d9ea;
}
#RightRailButton:hover, #RightRailToggle:hover {
    background-color: #23394a; border-color: #3c6078;
}
#RightRailButton:checked {
    background-color: #16475b; border: 1px solid #227b96; border-left: 3px solid #38d6ee;
}
#RightRailButton:checked:hover { background-color: #1c566d; }
#RightRailToggle { color: #aab6c3; font-size: 16px; font-weight: 600; }
#RightToolRailSep { background-color: #343b45; border: none; max-height: 1px; }
#PropTitle {
    background-color: #1e2d41; color: #edf5ff; font-weight: 600; font-size: 15px;
    padding: 17px 16px; border-bottom: 1px solid #2b4059;
}
#PropHint { color: #9eb3ce; font-size: 11px; padding: 12px 16px; background-color: #192230; }
#PropLabel { color: #a2b6cf; font-size: 11px; padding-top: 5px; padding-bottom: 2px; }
#ApplyBtn, QPushButton[primary="true"], QPushButton:default {
    background-color: #1b6bd1; color: #ffffff; border: 1px solid #459afa;
    border-radius: 8px; padding: 10px 14px; font-weight: 600;
}
#ApplyBtn:hover, QPushButton[primary="true"]:hover, QPushButton:default:hover {
    background-color: #1672df; border-color: #83c4ff;
}
#ApplyBtn:pressed, QPushButton[primary="true"]:pressed, QPushButton:default:pressed { background-color: #1459ac; }
#ApplyBtn:disabled, QPushButton[primary="true"]:disabled { background-color: #293e57; color: #718ba8; border-color: #344c68; }
#SimulatePathBtn {
    background-color: #244a3c; color: #dfffee; border: 1px solid #3d7f66;
    border-radius: 8px; padding: 9px 13px; font-weight: 600;
}
#SimulatePathBtn:hover { background-color: #2d6651; border-color: #64ba94; }
#SimulatePathBtn:pressed { background-color: #22503f; }
#SimulationProgress {
    min-height: 18px; max-height: 18px;
    background-color: #141e2b; border: 1px solid #344760; border-radius: 5px;
    text-align: center; color: #dfffee;
}
#SimulationProgress::chunk {
    background-color: #2d8b68; border-radius: 4px;
}
#AnchorDot::indicator { width: 14px; height: 14px; }

QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
    background-color: #141e2b; border: 1px solid #344760; border-radius: 7px;
    padding: 7px 9px; color: #e6edf6; selection-background-color: #2269b1; min-height: 17px;
}
QComboBox:hover, QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover { border-color: #4b6b90; }
QComboBox:focus, QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #58a6ff; }
QComboBox:disabled, QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
    color: #62758f; border-color: #293b51; background-color: #1a2534;
}
QComboBox { padding-right: 25px; }
QComboBox::drop-down { border: none; width: 25px; }
QComboBox::down-arrow { image: url(__ASSETS__/chevron-down.svg); width: 12px; height: 12px; }
QComboBox QAbstractItemView {
    background-color: #1d2a3b; color: #dce9f8; selection-background-color: #244f7b;
    border: 1px solid #3b526e; padding: 5px; outline: none;
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
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background-color: #28425f; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(__ASSETS__/chevron-up.svg); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(__ASSETS__/chevron-down.svg); width: 10px; height: 10px; }
QCheckBox, QRadioButton { color: #c4d4e7; spacing: 8px; padding: 3px 0; }
QCheckBox::indicator, QRadioButton::indicator {
    width: 15px; height: 15px; background-color: #141e2b; border: 1px solid #526c8a; border-radius: 4px;
}
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: #7dbdff; }
QCheckBox::indicator:checked { background-color: #2589f5; border-color: #6eb7ff; image: url(__ASSETS__/check.svg); }
QRadioButton::indicator { border-radius: 8px; }
QRadioButton::indicator:checked { background-color: #2589f5; border-color: #80c2ff; image: url(__ASSETS__/radio-dot.svg); }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { background-color: #202d3f; border-color: #3b4e66; }
QGroupBox {
    background-color: #1b293b; border: 1px solid #30445e; border-radius: 10px;
    margin-top: 15px; padding-top: 13px; font-weight: 600;
}
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #88c3ff; }
#GroupCollapseBtn {
    background: transparent; color: #7cb8f7; border: 1px solid #2d4562;
    border-radius: 4px; padding: 2px 8px; font-size: 11px; font-weight: 500;
}
#GroupCollapseBtn:hover { background-color: #243851; color: #ffffff; border-color: #589bdf; }
#GroupCollapseBtn:pressed { background-color: #1c2e44; }
#SectionNavPill {
    background-color: #1a2737; color: #9bbcdb; border: 1px solid #2d415b;
    border-radius: 12px; padding: 3px 10px; font-size: 11px; font-weight: 500;
}
#SectionNavPill:hover { background-color: #263e5b; color: #ffffff; border-color: #498ad0; }
#SectionNavPill:pressed { background-color: #1e5a96; }
QPushButton {
    background-color: #24364b; border: 1px solid #3b536e; border-radius: 7px;
    padding: 8px 13px; color: #dce8f6;
}
QPushButton:hover { background-color: #2c4664; border-color: #5e90c7; }
QPushButton:pressed { background-color: #235b96; }
QPushButton:focus { border-color: #91ccff; }
QPushButton:disabled { color: #62758f; background-color: #1e2a3a; border-color: #2b3c52; }

#ViewTabs { background-color: #141b25; border-bottom: 1px solid #2b3b50; }
#ViewTabs::tab {
    background-color: #141b25; color: #8ea5c1; padding: 12px 24px;
    border: none; border-bottom: 2px solid transparent; min-width: 112px;
}
#ViewTabs::tab:selected { color: #d5ebff; background-color: #223650; border-bottom: 2px solid #49a2ff; }
#ViewTabs::tab:hover { color: #e6f3ff; background-color: #1e3048; }
#View3DControls { background-color: #141b25; border: none; }
#View3DBtn {
    background-color: #1d2a3a; color: #b8cbe0; border: 1px solid #31445b;
    border-radius: 5px; padding: 0 7px; font-size: 10px; font-weight: 600;
}
#View3DBtn:hover { background-color: #263f5b; color: #ffffff; border-color: #5797d2; }
#View3DBtn:pressed { background-color: #1f70c5; border-color: #70b7fa; }
#BottomTabs { background-color: #141e2c; border-top: 1px solid #2b3b50; }
#BottomTabs::tab { color: #8ca3bf; padding: 12px 0; border: none; border-top: 2px solid transparent; }
#BottomTabs::tab:selected { color: #8fc8ff; background-color: #20374f; border-top: 2px solid #2589f5; }
#BottomTabs::tab:hover { background-color: #243851; color: #dcecff; }
#ToolPanel[compact="true"] #BottomTabs::tab { padding: 4px 0; font-size: 11px; }
#ToolPanel[compact="true"] #MaterialBtn { padding: 4px 8px; }
#ToolPanel[compact="true"] QComboBox { padding-top: 4px; padding-bottom: 4px; }

QStatusBar { background-color: #172d47; color: #a9c8e9; font-size: 11px; border-top: 1px solid #294d73; padding: 3px 10px; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #a9c8e9; padding: 0 6px; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: transparent; width: 7px; margin: 3px 0; }
QScrollBar::handle:vertical { background: #354b65; border-radius: 3px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #5c7fa8; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
)QSS").replace("__ASSETS__", ":/assets");
    return sheet;
}

}  // namespace cnc::ui
