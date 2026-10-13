// Alteracao reversivel do trabalho, incluindo escala dos vetores (porte de src/cad/job_commands.py).
#pragma once

#include <vector>

#include "cad/document.hpp"
#include "utils/commands.hpp"

namespace cnc::cad {

// Copias dos vetores escalados em (sx, sy) em torno de (cx, cy). Entidades que nao representam
// uma transformacao nao uniforme exatamente (circulo, arco, texto, elipse/retangulo girados ou
// com canto arredondado) viram contorno, em vez de deformar os parametros.
std::vector<EntityPtr> scaled_entities(const std::vector<EntityPtr>& entities, double sx, double sy,
                                       int segments, double cx = 0.0, double cy = 0.0);

class JobSetupCommand : public Command {
public:
    // `document` precisa sobreviver ao comando (o historico de undo guarda so a referencia).
    JobSetupCommand(Document& document, const MaterialSetup& material);

    void execute() override { apply(after_material_, after_entities_); }
    void undo() override { apply(before_material_, before_entities_); }
    std::string description() const override { return "Definições do Trabalho"; }

private:
    void apply(const MaterialSetup& material, const std::vector<EntityPtr>& entities);

    Document& document_;
    MaterialSetup before_material_;
    MaterialSetup after_material_;
    std::vector<EntityPtr> before_entities_;
    std::vector<EntityPtr> after_entities_;
};

}  // namespace cnc::cad
