// Ponto de entrada dos testes (doctest + Qt).
//
// Regra herdada do app em Python: processo de teste NUNCA escreve na pasta real
// de configuracao (~/.cnc-cam). Ja aconteceu de uma bateria de testes desligar o
// snap do usuario. Por isso a pasta temporaria e definida aqui, antes de qualquer
// codigo do app rodar, e nao depende de quem chama o executavel.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <QApplication>
#include <QTemporaryDir>

int main(int argc, char** argv) {
    QTemporaryDir config_dir;
    if (!config_dir.isValid()) return 2;
    qputenv("CNC_CAM_CONFIG", config_dir.path().toUtf8());

    QApplication app(argc, argv);
    app.setApplicationName("CNC-CAM");
    app.setOrganizationName("UNY");

    doctest::Context ctx(argc, argv);
    return ctx.run();
}
