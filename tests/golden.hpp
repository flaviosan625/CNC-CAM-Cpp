// Comparacao com os "golden files" gerados pelo app em Python (scripts/golden).
#pragma once

#include <doctest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "cad/document.hpp"
#include "utils/json_util.hpp"

namespace cnc::testing {

// Tolerancia numerica da comparacao C++ x Python. As contas sao as mesmas, na mesma
// ordem; so se admite o ruido de uma ou duas casas na ultima unidade.
inline constexpr double kTol = 1e-9;

inline std::filesystem::path golden_dir() {
    return std::filesystem::path(CNC_SOURCE_DIR) / "tests" / "golden";
}

inline bool golden_exists(const std::string& file) {
    return std::filesystem::exists(golden_dir() / file);
}

inline Json load_golden(const std::string& file) {
    return Json::parse(read_text_file(golden_dir() / file));
}

inline bool near(double a, double b, double tol = kTol) {
    if (a == b) return true;
    return std::fabs(a - b) <= tol * (1.0 + std::max(std::fabs(a), std::fabs(b)));
}

// Descreve a primeira diferenca entre os movimentos, ou nada se forem iguais.
inline std::optional<std::string> diff_moves(const std::vector<cam::Move>& got, const Json& want) {
    std::ostringstream os;
    if (got.size() != want.size()) {
        os << "numero de movimentos: C++ " << got.size() << " x Python " << want.size();
        // ainda mostra onde comecam a divergir, ajuda a achar o bug
        const size_t n = std::min(got.size(), want.size());
        for (size_t i = 0; i < n; ++i) {
            const Json& w = want[i];
            if (std::string(to_str(got[i].kind)) != w[0].get<std::string>() ||
                !near(got[i].x, w[1].get<double>()) || !near(got[i].y, w[2].get<double>()) ||
                !near(got[i].z, w[3].get<double>())) {
                os << "; primeira diferenca no movimento " << i;
                break;
            }
        }
        return os.str();
    }
    for (size_t i = 0; i < got.size(); ++i) {
        const Json& w = want[i];
        const std::string kind = w[0].get<std::string>();
        const double wx = w[1].get<double>(), wy = w[2].get<double>(), wz = w[3].get<double>();
        const double ws = w[4].get<double>();
        if (kind != to_str(got[i].kind) || !near(got[i].x, wx) || !near(got[i].y, wy) ||
            !near(got[i].z, wz) || !near(got[i].seconds, ws)) {
            os << "movimento " << i << ": C++ (" << to_str(got[i].kind) << ", " << got[i].x << ", "
               << got[i].y << ", " << got[i].z << ", " << got[i].seconds << ") x Python (" << kind << ", "
               << wx << ", " << wy << ", " << wz << ", " << ws << ")";
            return os.str();
        }
    }
    return std::nullopt;
}

// Primeira linha em que dois textos divergem, para a mensagem de erro. Com CNC_DIFF_DIR no ambiente,
// grava tambem os dois textos inteiros (<nome>.cpp.txt e <nome>.py.txt) para comparar com um diff.
inline std::string text_diff(const std::string& name, const std::string& got, const std::string& want) {
    if (const char* dir = std::getenv("CNC_DIFF_DIR"); dir && *dir) {
        write_text_file(std::filesystem::path(dir) / (name + ".cpp.txt"), got);
        write_text_file(std::filesystem::path(dir) / (name + ".py.txt"), want);
    }
    size_t line = 1;
    for (size_t i = 0; i < std::min(got.size(), want.size()); ++i) {
        if (got[i] != want[i]) {
            const size_t ga = got.rfind('\n', i), wa = want.rfind('\n', i);
            const size_t a = ga == std::string::npos ? 0 : ga + 1;
            const size_t b = wa == std::string::npos ? 0 : wa + 1;
            return "linha " + std::to_string(line) + ": C++ [" + got.substr(a, got.find('\n', i) - a) +
                   "] x Python [" + want.substr(b, want.find('\n', i) - b) + "]";
        }
        if (got[i] == '\n') ++line;
    }
    return "tamanhos diferentes: C++ " + std::to_string(got.size()) + " x Python " + std::to_string(want.size());
}

inline std::vector<std::string> strings_of(const Json& arr) {
    std::vector<std::string> out;
    for (const Json& s : arr) out.push_back(s.get<std::string>());
    return out;
}

inline std::string norm_term(std::string s) {
    size_t pos = 0;
    while ((pos = s.find("Desbaste", pos)) != std::string::npos) {
        s.replace(pos, 8, "Bolso");
        pos += 5;
    }
    pos = 0;
    while ((pos = s.find("desbaste da base", pos)) != std::string::npos) {
        s.replace(pos, 16, "bolso");
        pos += 5;
    }
    return s;
}

// Roda todos os casos de um golden file de percurso. Devolve quantos casos conferiram.
inline int check_cam_golden(const std::string& file) {
    const Json golden = load_golden(file);
    int checked = 0;
    for (const Json& c : golden.at("cases")) {
        const std::string name = c.at("name").get<std::string>();
        INFO("arquivo: " << file << " | caso: " << name);

        cad::Document doc;
        doc.from_json(c.at("document"));
        const std::vector<std::string> raw_errors = doc.recalculate_toolpaths();
        std::vector<std::string> errors;
        for (const auto& e : raw_errors) errors.push_back(norm_term(e));
        const Json& expected = c.at("expected");

        CHECK_MESSAGE(errors == strings_of(expected.at("errors")), "erros do documento");
        const Json& jobs = expected.at("jobs");
        REQUIRE_MESSAGE(doc.toolpaths.size() == jobs.size(), "numero de percursos");
        for (size_t j = 0; j < jobs.size(); ++j) {
            const auto& job = *doc.toolpaths[j];
            const Json& want = jobs[j];
            INFO("percurso: " << job.name);
            CHECK_MESSAGE(job.error == want.at("error").get<std::string>(), "mensagem de erro do percurso");
            CHECK_MESSAGE(job.summary() == want.at("summary").get<std::string>(), "resumo do percurso");
            CHECK_MESSAGE(job.all_notes() == strings_of(want.at("notes")), "avisos do percurso");

            const Json& tps = want.at("toolpaths");
            REQUIRE_MESSAGE(job.results.size() == tps.size(), "numero de trajetorias");
            for (size_t t = 0; t < tps.size(); ++t) {
                const cam::Toolpath& got = job.results[t];
                const Json& w = tps[t];
                INFO("trajetoria: " << got.name);
                CHECK(norm_term(got.name) == norm_term(w.at("name").get<std::string>()));
                CHECK(std::string(to_str(got.kind)) == w.at("kind").get<std::string>());
                CHECK(got.tool_name == w.at("tool_name").get<std::string>());
                CHECK(got.tool_number == w.at("tool_number").get<int>());
                CHECK(got.spindle_speed == w.at("spindle_speed").get<int>());
                CHECK(near(got.feed_rate, w.at("feed_rate").get<double>()));
                CHECK(near(got.plunge_rate, w.at("plunge_rate").get<double>()));
                CHECK(got.notes == strings_of(w.at("notes")));
                CHECK_MESSAGE(got.summary() == w.at("summary").get<std::string>(), "resumo");
                if (auto d = diff_moves(got.moves, w.at("moves"))) CHECK_MESSAGE(false, *d);
            }
        }
        ++checked;
    }
    return checked;
}

}  // namespace cnc::testing
