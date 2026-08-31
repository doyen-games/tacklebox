#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>

// Point the app data dir at a scratch location BEFORE anything calls
// tb::dataDir() (it caches). Keeps vault tests away from a real vault.
int main(int argc, char** argv) {
    auto sandbox = std::filesystem::temp_directory_path() / "tacklebox-tests";
    std::filesystem::create_directories(sandbox);
#ifdef _WIN32
    _putenv_s("APPDATA", sandbox.string().c_str());
#else
    setenv("XDG_DATA_HOME", sandbox.string().c_str(), 1);
    setenv("HOME", sandbox.string().c_str(), 1);
#endif
    return doctest::Context(argc, argv).run();
}
