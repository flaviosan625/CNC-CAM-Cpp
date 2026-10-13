#include "cad/doc_vars.hpp"

#include <format>

#include <QDate>
#include <QRegularExpression>
#include <QString>

namespace cnc::cad {

namespace {

// Um nome entre barras: |TRABALHO|. Barra e rara em texto de peca.
const QRegularExpression& token_re() {
    static const QRegularExpression re(QStringLiteral(R"(\|([A-Za-z0-9_\x{C0}-\x{FF} ]{1,40})\|)"));
    return re;
}

// f"{valor:.3f}".rstrip("0").rstrip(".")
std::string numero(double v) {
    std::string s = std::format("{:.3f}", v);
    while (!s.empty() && s.back() == '0') s.pop_back();
    while (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

}  // namespace

OrderedStrMap builtin_variables(const Document& document) {
    const MaterialSetup& m = document.material;
    OrderedStrMap out;
    out.set("TRABALHO", document.title());
    out.set("DATA", QDate::currentDate().toString("dd/MM/yyyy").toStdString());
    out.set("LARGURA", numero(m.width));
    out.set("ALTURA", numero(m.height));
    out.set("ESPESSURA", numero(m.thickness));
    out.set("MATERIAL", numero(m.width) + " × " + numero(m.height) + " × " + numero(m.thickness) + " mm");
    return out;
}

OrderedStrMap all_variables(const Document& document) {
    OrderedStrMap valores = builtin_variables(document);
    for (const auto& [k, v] : document.variables) valores.set(k, v);
    return valores;
}

std::string expand(const std::string& texto, const OrderedStrMap& valores) {
    const QString in = QString::fromStdString(texto);
    QString out;
    qsizetype last = 0;
    auto it = token_re().globalMatch(in);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += in.mid(last, m.capturedStart() - last);
        const std::string nome = m.captured(1).trimmed().toStdString();
        const std::string* v = valores.get(nome);
        if (!v) v = valores.get(QString::fromStdString(nome).toUpper().toStdString());
        out += v ? QString::fromStdString(*v) : m.captured(0);
        last = m.capturedEnd();
    }
    out += in.mid(last);
    return out.toStdString();
}

std::vector<std::string> tokens_in(const std::string& texto) {
    std::vector<std::string> out;
    const QString in = QString::fromStdString(texto);
    auto it = token_re().globalMatch(in);
    while (it.hasNext()) out.push_back(it.next().captured(1).trimmed().toStdString());
    return out;
}

}  // namespace cnc::cad
