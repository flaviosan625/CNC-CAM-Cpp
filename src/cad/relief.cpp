#include "cad/relief.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <QByteArray>
#include <QImage>

namespace cnc::cad::relief {

namespace {
constexpr double kPi = 3.14159265358979323846;

inline double clamp01(double v) {
    return std::clamp(v, 0.0, 1.0);
}

} // namespace

float LocalGrid::sample_bilinear(double x, double y) const {
    if (empty()) return 0.0f;
    const double u = (x - x0) / (step > 1e-6 ? step : 0.5);
    const double v = (y - y0) / (step > 1e-6 ? step : 0.5);

    const int c0 = std::clamp(static_cast<int>(std::floor(u)), 0, cols - 1);
    const int r0 = std::clamp(static_cast<int>(std::floor(v)), 0, rows - 1);
    const int c1 = std::min(c0 + 1, cols - 1);
    const int r1 = std::min(r0 + 1, rows - 1);

    const float fx = static_cast<float>(u - std::floor(u));
    const float fy = static_cast<float>(v - std::floor(v));

    const float z00 = get(c0, r0);
    const float z10 = get(c1, r0);
    const float z01 = get(c0, r1);
    const float z11 = get(c1, r1);

    const float z_top = z00 * (1.0f - fx) + z10 * fx;
    const float z_bot = z01 * (1.0f - fx) + z11 * fx;
    return z_top * (1.0f - fy) + z_bot * fy;
}

float ReliefComponent::sample_normalized(double u, double v) const {
    if (!has_image()) return 0.0f;
    u = clamp01(u);
    v = clamp01(v);

    const int px = std::clamp(static_cast<int>(u * (img_width - 1)), 0, img_width - 1);
    const int py = std::clamp(static_cast<int>(v * (img_height - 1)), 0, img_height - 1);
    const size_t idx = static_cast<size_t>(py * img_width + px);

    float raw_val = 0.0f;
    if (is_16bit && idx < raw_16bit.size()) {
        raw_val = static_cast<float>(raw_16bit[idx]) / 65535.0f;
    } else if (idx < raw_8bit.size()) {
        raw_val = static_cast<float>(raw_8bit[idx]) / 255.0f;
    }

    if (invert) {
        raw_val = 1.0f - raw_val;
    }

    if (std::abs(contrast - 1.0) > 0.01 && contrast > 0.01) {
        // Ajuste de contraste centrado em 0.5
        raw_val = static_cast<float>(std::clamp((raw_val - 0.5) * contrast + 0.5, 0.0, 1.0));
    }

    return std::clamp(raw_val, 0.0f, 1.0f);
}

bool ReliefComponent::load_from_memory(const uint8_t* data, size_t size, const std::string& filename, std::string* error) {
    if (!data || size == 0) {
        if (error) *error = "Buffer de imagem vazio.";
        return false;
    }

    QImage qimg;
    if (!qimg.loadFromData(data, static_cast<int>(size))) {
        if (error) *error = "Não foi possível decodificar os dados da imagem.";
        return false;
    }

    img_width = qimg.width();
    img_height = qimg.height();
    original_filename = filename;
    original_encoded_base64 = QByteArray(reinterpret_cast<const char*>(data), static_cast<int>(size)).toBase64().toStdString();

    // Verificação de precisão de 16-bit nativa do Qt
    if (qimg.format() == QImage::Format_Grayscale16) {
        is_16bit = true;
        raw_16bit.resize(static_cast<size_t>(img_width * img_height));
        raw_8bit.clear();
        for (int y = 0; y < img_height; ++y) {
            const auto* line = reinterpret_cast<const uint16_t*>(qimg.constScanLine(y));
            for (int x = 0; x < img_width; ++x) {
                raw_16bit[static_cast<size_t>(y * img_width + x)] = line[x];
            }
        }
    } else {
        is_16bit = false;
        QImage gray = qimg.convertToFormat(QImage::Format_Grayscale8);
        raw_8bit.resize(static_cast<size_t>(img_width * img_height));
        raw_16bit.clear();
        for (int y = 0; y < img_height; ++y) {
            const auto* line = gray.constScanLine(y);
            for (int x = 0; x < img_width; ++x) {
                raw_8bit[static_cast<size_t>(y * img_width + x)] = line[x];
            }
        }
    }
    return true;
}

bool ReliefComponent::load_from_file(const std::string& filepath, std::string* error) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in) {
        if (error) *error = "Não foi possível abrir o arquivo: " + filepath;
        return false;
    }
    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return load_from_memory(buffer.data(), buffer.size(), filepath, error);
}

nlohmann::json ReliefComponent::to_json() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["visible"] = visible;
    j["locked"] = locked;
    j["pos_x"] = pos_x;
    j["pos_y"] = pos_y;
    j["width"] = width;
    j["height"] = height;
    j["shape_height"] = shape_height;
    j["base_height"] = base_height;
    j["smoothness"] = smoothness;
    j["contrast"] = contrast;
    j["invert"] = invert;
    j["combine_mode"] = ReliefEngine::combine_mode_name(combine_mode);
    j["nature"] = (nature == ReliefNature::Raised) ? "raised" : "carved";
    j["vector_mask_id"] = vector_mask_id;
    j["is_16bit"] = is_16bit;
    j["img_width"] = img_width;
    j["img_height"] = img_height;
    j["original_filename"] = original_filename;
    j["original_encoded_base64"] = original_encoded_base64;
    return j;
}

ReliefComponent ReliefComponent::from_json(const nlohmann::json& j) {
    ReliefComponent c;
    c.id = j.value("id", "");
    c.name = j.value("name", "Relevo");
    c.visible = j.value("visible", true);
    c.locked = j.value("locked", false);
    c.pos_x = j.value("pos_x", 0.0);
    c.pos_y = j.value("pos_y", 0.0);
    c.width = j.value("width", 100.0);
    c.height = j.value("height", 100.0);
    c.shape_height = j.value("shape_height", 3.0);
    c.base_height = j.value("base_height", 0.0);
    c.smoothness = j.value("smoothness", 0.0);
    c.contrast = j.value("contrast", 1.0);
    c.invert = j.value("invert", false);
    c.combine_mode = ReliefEngine::parse_combine_mode(j.value("combine_mode", "adicionar"));
    c.nature = (j.value("nature", "raised") == "carved") ? ReliefNature::Carved : ReliefNature::Raised;
    c.vector_mask_id = j.value("vector_mask_id", "");
    c.original_filename = j.value("original_filename", "");
    c.original_encoded_base64 = j.value("original_encoded_base64", "");

    if (!c.original_encoded_base64.empty()) {
        QByteArray raw = QByteArray::fromBase64(QByteArray::fromStdString(c.original_encoded_base64));
        c.load_from_memory(reinterpret_cast<const uint8_t*>(raw.constData()), static_cast<size_t>(raw.size()), c.original_filename);
    }
    return c;
}

const char* ReliefEngine::combine_mode_name(CombineMode m) {
    switch (m) {
        case CombineMode::Add: return "adicionar";
        case CombineMode::Subtract: return "subtrair";
        case CombineMode::MergeHigh: return "mesclar_alto";
        case CombineMode::MergeLow: return "mesclar_baixo";
        case CombineMode::Multiply: return "multiplicar";
    }
    return "adicionar";
}

CombineMode ReliefEngine::parse_combine_mode(const std::string& s) {
    if (s == "subtrair" || s == "subtract") return CombineMode::Subtract;
    if (s == "mesclar_alto" || s == "merge_high" || s == "merge") return CombineMode::MergeHigh;
    if (s == "mesclar_baixo" || s == "merge_low") return CombineMode::MergeLow;
    if (s == "multiplicar" || s == "multiply") return CombineMode::Multiply;
    return CombineMode::Add;
}

void ReliefEngine::generate_component_grid(
    const ReliefComponent& comp,
    double resolution_mm,
    LocalGrid& out_grid
) {
    if (comp.width <= 0.01 || comp.height <= 0.01 || !comp.has_image()) {
        out_grid = LocalGrid{};
        return;
    }

    const double res = std::max(0.05, resolution_mm);
    const int cols = std::max(2, static_cast<int>(std::ceil(comp.width / res)) + 1);
    const int rows = std::max(2, static_cast<int>(std::ceil(comp.height / res)) + 1);

    out_grid.x0 = comp.pos_x;
    out_grid.y0 = comp.pos_y;
    out_grid.width = comp.width;
    out_grid.height = comp.height;
    out_grid.step = res;
    out_grid.cols = cols;
    out_grid.rows = rows;
    out_grid.z.assign(static_cast<size_t>(cols * rows), 0.0f);

    const double sign = (comp.nature == ReliefNature::Carved) ? -1.0 : 1.0;
    const double effective_shape = comp.shape_height * sign;

    for (int r = 0; r < rows; ++r) {
        const double v = static_cast<double>(r) / std::max(1, rows - 1);
        for (int c = 0; c < cols; ++c) {
            const double u = static_cast<double>(c) / std::max(1, cols - 1);
            const float norm = comp.sample_normalized(u, v);
            const float z_val = static_cast<float>(comp.base_height + norm * effective_shape);
            out_grid.set(c, r, z_val);
        }
    }

    // Suavização Gaussiana separável se solicitada
    if (comp.smoothness > 0.01) {
        const int radius = std::clamp(static_cast<int>(comp.smoothness * 8.0), 1, 12);
        std::vector<float> temp = out_grid.z;

        // Passada horizontal
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float sum = 0.0f;
                int count = 0;
                for (int dc = -radius; dc <= radius; ++dc) {
                    const int nc = c + dc;
                    if (nc >= 0 && nc < cols) {
                        sum += out_grid.get(nc, r);
                        count++;
                    }
                }
                temp[static_cast<size_t>(r * cols + c)] = (count > 0) ? (sum / count) : out_grid.get(c, r);
            }
        }

        // Passada vertical
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                float sum = 0.0f;
                int count = 0;
                for (int dr = -radius; dr <= radius; ++dr) {
                    const int nr = r + dr;
                    if (nr >= 0 && nr < rows) {
                        sum += temp[static_cast<size_t>(nr * cols + c)];
                        count++;
                    }
                }
                out_grid.set(c, r, (count > 0) ? (sum / count) : temp[static_cast<size_t>(r * cols + c)]);
            }
        }
    }
}

void ReliefEngine::compose(
    const std::vector<ReliefComponent>& components,
    double workpiece_w,
    double workpiece_h,
    double resolution_mm,
    LocalGrid& out_composite,
    double max_total_height
) {
    const double res = std::max(0.05, resolution_mm);
    const double w = std::max(1.0, workpiece_w);
    const double h = std::max(1.0, workpiece_h);

    const int cols = std::max(2, static_cast<int>(std::ceil(w / res)) + 1);
    const int rows = std::max(2, static_cast<int>(std::ceil(h / res)) + 1);

    out_composite.x0 = 0.0;
    out_composite.y0 = 0.0;
    out_composite.width = w;
    out_composite.height = h;
    out_composite.step = res;
    out_composite.cols = cols;
    out_composite.rows = rows;
    out_composite.z.assign(static_cast<size_t>(cols * rows), 0.0f);

    for (const auto& comp : components) {
        if (!comp.visible || !comp.has_image()) continue;

        LocalGrid comp_grid;
        generate_component_grid(comp, res, comp_grid);
        if (comp_grid.empty()) continue;

        const int start_c = std::clamp(static_cast<int>(std::floor(comp.pos_x / res)), 0, cols - 1);
        const int end_c   = std::clamp(static_cast<int>(std::ceil((comp.pos_x + comp.width) / res)), 0, cols - 1);
        const int start_r = std::clamp(static_cast<int>(std::floor(comp.pos_y / res)), 0, rows - 1);
        const int end_r   = std::clamp(static_cast<int>(std::ceil((comp.pos_y + comp.height) / res)), 0, rows - 1);

        for (int r = start_r; r <= end_r; ++r) {
            const double y = r * res;
            for (int c = start_c; c <= end_c; ++c) {
                const double x = c * res;
                if (x < comp.pos_x || x > comp.pos_x + comp.width ||
                    y < comp.pos_y || y > comp.pos_y + comp.height) {
                    continue;
                }

                const float base_z = out_composite.get(c, r);
                const float comp_z = comp_grid.sample_bilinear(x, y);
                float final_z = base_z;

                switch (comp.combine_mode) {
                    case CombineMode::Add:
                        final_z = base_z + comp_z;
                        break;
                    case CombineMode::Subtract:
                        final_z = base_z - comp_z;
                        break;
                    case CombineMode::MergeHigh:
                        final_z = std::max(base_z, comp_z);
                        break;
                    case CombineMode::MergeLow:
                        final_z = std::min(base_z, comp_z);
                        break;
                    case CombineMode::Multiply: {
                        const double ref = (std::abs(comp.shape_height) > 0.01) ? std::abs(comp.shape_height) : 1.0;
                        final_z = static_cast<float>((base_z * comp_z) / ref);
                        break;
                    }
                }

                // Trava de segurança da altura total máxima configurável (200 mm)
                final_z = std::clamp(final_z, static_cast<float>(-max_total_height), static_cast<float>(max_total_height));
                out_composite.set(c, r, final_z);
            }
        }
    }
}

cad::Mesh ReliefEngine::generate_mesh(
    const LocalGrid& grid,
    double base_z,
    double light_azimuth_deg,
    double light_elevation_deg
) {
    cad::Mesh mesh;
    if (grid.empty()) return mesh;

    const int cols = grid.cols;
    const int rows = grid.rows;
    const size_t vertex_count = static_cast<size_t>(cols * rows);
    mesh.vertices.reserve(vertex_count * 6);

    // Vetor de luz direcional
    const double az_rad = light_azimuth_deg * kPi / 180.0;
    const double el_rad = light_elevation_deg * kPi / 180.0;
    const double lx = std::cos(el_rad) * std::cos(az_rad);
    const double ly = std::cos(el_rad) * std::sin(az_rad);
    const double lz = std::sin(el_rad);

    for (int r = 0; r < rows; ++r) {
        const double y = grid.y0 + r * grid.step;
        for (int c = 0; c < cols; ++c) {
            const double x = grid.x0 + c * grid.step;
            const float z = grid.get(c, r) + static_cast<float>(base_z);

            // Cálculo das normais por diferenças centrais
            const float z_l = (c > 0) ? grid.get(c - 1, r) : z;
            const float z_r = (c + 1 < cols) ? grid.get(c + 1, r) : z;
            const float z_b = (r > 0) ? grid.get(c, r - 1) : z;
            const float z_t = (r + 1 < rows) ? grid.get(c, r + 1) : z;

            const double dz_dx = (z_r - z_l) / (2.0 * grid.step);
            const double dz_dy = (z_t - z_b) / (2.0 * grid.step);

            double nx = -dz_dx;
            double ny = -dz_dy;
            double nz = 1.0;
            const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-7) { nx /= len; ny /= len; nz /= len; }

            const double dot = std::max(0.15, nx * lx + ny * ly + nz * lz);
            const float shade = static_cast<float>(dot);

            // Tonalidade quente de madeira/entalhe para realce de relevo
            const float cr = std::clamp(0.85f * shade, 0.0f, 1.0f);
            const float cg = std::clamp(0.72f * shade, 0.0f, 1.0f);
            const float cb = std::clamp(0.55f * shade, 0.0f, 1.0f);

            mesh.vertices.insert(mesh.vertices.end(), {
                static_cast<float>(x),
                static_cast<float>(y),
                z,
                cr, cg, cb
            });
        }
    }

    const size_t quad_count = static_cast<size_t>((cols - 1) * (rows - 1));
    mesh.indices.reserve(quad_count * 6);

    for (int r = 0; r < rows - 1; ++r) {
        for (int c = 0; c < cols - 1; ++c) {
            const unsigned int idx00 = static_cast<unsigned int>(r * cols + c);
            const unsigned int idx10 = idx00 + 1;
            const unsigned int idx01 = static_cast<unsigned int>((r + 1) * cols + c);
            const unsigned int idx11 = idx01 + 1;

            mesh.indices.insert(mesh.indices.end(), {
                idx00, idx10, idx11,
                idx00, idx11, idx01
            });
        }
    }

    return mesh;
}

void ReliefEngine::apply_sculpt_stroke(
    LocalGrid& grid,
    SculptTool tool,
    double cx,
    double cy,
    double radius_mm,
    double strength,
    double max_total_height
) {
    if (grid.empty() || radius_mm <= 0.01) return;

    const double rad_sq = radius_mm * radius_mm;
    const int start_c = std::clamp(static_cast<int>(std::floor((cx - radius_mm - grid.x0) / grid.step)), 0, grid.cols - 1);
    const int end_c   = std::clamp(static_cast<int>(std::ceil((cx + radius_mm - grid.x0) / grid.step)), 0, grid.cols - 1);
    const int start_r = std::clamp(static_cast<int>(std::floor((cy - radius_mm - grid.y0) / grid.step)), 0, grid.rows - 1);
    const int end_r   = std::clamp(static_cast<int>(std::ceil((cy + radius_mm - grid.y0) / grid.step)), 0, grid.rows - 1);

    for (int r = start_r; r <= end_r; ++r) {
        const double y = grid.y0 + r * grid.step;
        const double dy = y - cy;
        for (int c = start_c; c <= end_c; ++c) {
            const double x = grid.x0 + c * grid.step;
            const double dx = x - cx;
            const double dist_sq = dx * dx + dy * dy;
            if (dist_sq > rad_sq) continue;

            const double norm_dist = std::sqrt(dist_sq) / radius_mm;
            const double falloff = std::pow(1.0 - norm_dist * norm_dist, 2.0); // Quartic falloff

            float z = grid.get(c, r);
            switch (tool) {
                case SculptTool::Elevate:
                    z += static_cast<float>(strength * falloff);
                    break;
                case SculptTool::Remove:
                    z -= static_cast<float>(strength * falloff);
                    break;
                case SculptTool::Smooth: {
                    float sum = 0.0f;
                    int count = 0;
                    for (int dr = -1; dr <= 1; ++dr) {
                        for (int dc = -1; dc <= 1; ++dc) {
                            const int nc = c + dc, nr = r + dr;
                            if (nc >= 0 && nc < grid.cols && nr >= 0 && nr < grid.rows) {
                                sum += grid.get(nc, nr);
                                count++;
                            }
                        }
                    }
                    const float avg = (count > 0) ? (sum / count) : z;
                    z = static_cast<float>(z * (1.0 - falloff * strength) + avg * (falloff * strength));
                    break;
                }
                case SculptTool::Smudge:
                    // Esfumar: arrasto na direção local
                    z += static_cast<float>(strength * falloff * 0.5);
                    break;
            }

            z = std::clamp(z, static_cast<float>(-max_total_height), static_cast<float>(max_total_height));
            grid.set(c, r, z);
        }
    }
}

} // namespace cnc::cad::relief
