// CNC-CAM - software CAD/CAM para CNC estilo Aspire (porte de main.py).
//
//   CNC-CAM.exe [arquivo.cnc]
//   CNC-CAM.exe --grab saida.png [--3d] [--size 1400x820] (tira uma foto da janela e sai; serve para
//                                                     conferir a interface sem ninguem na frente)
#include <QApplication>
#include <QAbstractSpinBox>
#include <QCommandLineParser>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QTimer>

#include "app/main_window.hpp"
#include "ui/theme.hpp"
#include "ui/machining_guide.hpp"
#include "ui/safe_inputs.hpp"

int main(int argc, char** argv) {
    // cnc_gui is a static library; explicitly register its compiled-in qrc so
    // the QSS SVGs are available in the final executable as they are in Python.
    Q_INIT_RESOURCE(cnc_assets);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication app(argc, argv);
    auto* safe_wheel_filter = new cnc::ui::SafeWheelFilter(&app);
    app.installEventFilter(safe_wheel_filter);
    app.setApplicationName("CNC-CAM");
    app.setOrganizationName("UNY");
    app.setFont(QFont("Segoe UI"));
    app.setStyleSheet(cnc::ui::dark_theme());

    QCommandLineParser cli;
    cli.addPositionalArgument("arquivo", "Projeto .cnc para abrir", "[arquivo]");
    QCommandLineOption grab("grab", "Salva uma imagem da janela e encerra.", "png");
    QCommandLineOption view_3d("3d", "Abre a vista 3D antes da captura.");
    QCommandLineOption size("size", "Tamanho da janela (LxA).", "LxA");
    QCommandLineOption guides("guides", "Abre o estúdio dos desenhos interativos.");
    cli.addOption(grab);
    cli.addOption(view_3d);
    cli.addOption(size);
    cli.addOption(guides);
    cli.process(app);

    cnc::app::MainWindow window;
    const QStringList dims = cli.value(size).split('x');
    if (dims.size() == 2) window.resize(dims[0].toInt(), dims[1].toInt());
    if (!cli.positionalArguments().isEmpty()) window.open_file(cli.positionalArguments().first());
    window.show();
    if (cli.isSet(view_3d)) window.show_3d_view();
    if (cli.isSet(guides)) QTimer::singleShot(0, &window, [&window] { cnc::ui::showGuideGallery(&window); });

    if (cli.isSet(grab)) {
        const QString path = cli.value(grab);
        QTimer::singleShot(800, [&window, path, &app] {
            auto* modal = QApplication::activeModalWidget();
            (modal ? modal : static_cast<QWidget*>(&window))->grab().save(path);
            app.quit();
        });
    }
    return app.exec();
}
