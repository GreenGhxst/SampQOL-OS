#include "test_main.h"
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include "util/common.h"

std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }

TempDir::TempDir() {
    const char* base = std::getenv("TMPDIR");
    std::string tmpl = std::string(base ? base : "/tmp") + "/dmtest-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end()); buf.push_back(0);
    if (!::mkdtemp(buf.data())) throw std::runtime_error("mkdtemp failed");
    path = buf.data();
}
static void rm_rf(const std::string& p) {
    struct stat st{};
    if (::lstat(p.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        if (DIR* d = ::opendir(p.c_str())) {
            while (dirent* e = ::readdir(d)) { std::string n = e->d_name; if (n != "." && n != "..") rm_rf(p + "/" + n); }
            ::closedir(d);
        }
        ::rmdir(p.c_str());
    } else ::unlink(p.c_str());
}
TempDir::~TempDir() { if (!path.empty()) rm_rf(path); }
void TempDir::mkdir(const std::string& rel) const {
    std::string acc = path;
    std::size_t i = 0;
    while (i <= rel.size()) { auto j = rel.find('/', i); if (j == std::string::npos) j = rel.size(); acc += "/" + rel.substr(i, j - i); ::mkdir(acc.c_str(), 0755); i = j + 1; }
}
void TempDir::write(const std::string& rel, const std::string& content) const { std::ofstream f(sub(rel), std::ios::binary | std::ios::trunc); f << content; }
std::string TempDir::read(const std::string& rel) const { std::ifstream f(sub(rel), std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
bool TempDir::exists(const std::string& rel) const { struct stat st{}; return ::lstat(sub(rel).c_str(), &st) == 0; }

int main(int argc, char** argv) {
    dm::log_set_level(dm::LogLevel::error);
    std::string filter = argc > 1 ? argv[1] : "";
    int failed = 0, ran = 0;
    for (auto& t : registry()) {
        if (!filter.empty() && std::string(t.name).find(filter) == std::string::npos) continue;
        ++ran;
        try { t.fn(); std::printf("  ok   %s\n", t.name); }
        catch (const std::exception& e) { ++failed; std::printf("  FAIL %s\n       %s\n", t.name, e.what()); }
    }
    std::printf("%d test(s), %d failed\n", ran, failed);
    return failed ? 1 : 0;
}
