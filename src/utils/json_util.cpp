#include "utils/json_util.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace cnc {

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("nao foi possivel abrir " + path.string());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // BOM UTF-8, se houver (o Python com encoding="utf-8" nao o remove, mas o json.loads falharia)
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

void write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("nao foi possivel gravar " + path.string());
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

}  // namespace cnc
