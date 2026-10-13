#include "utils/user_config.hpp"

#include <atomic>
#include <cstdlib>
#include <mutex>

#include <QDir>
#include <QTemporaryDir>

namespace cnc::user_config {

namespace {
std::atomic<bool> g_under_test{false};
}

void mark_under_test() { g_under_test = true; }
bool running_under_test() { return g_under_test; }

std::filesystem::path config_dir() {
    if (const char* raiz = std::getenv(kEnvVar); raiz && *raiz) {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(raiz)));
    }
    if (running_under_test()) {
        // uma pasta temporaria por processo, removida ao sair
        static std::mutex mu;
        static QTemporaryDir* dir = nullptr;
        std::lock_guard<std::mutex> lock(mu);
        if (!dir) dir = new QTemporaryDir(QDir::tempPath() + "/cnc-cam-testes-XXXXXX");
        return std::filesystem::path(dir->path().toStdWString());
    }
    return std::filesystem::path(QDir::homePath().toStdWString()) / ".cnc-cam";
}

std::filesystem::path config_file(const std::string& name) {
    return config_dir() / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name.c_str())));
}

}  // namespace cnc::user_config
