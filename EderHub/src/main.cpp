#include "HubApp.h"
#include <filesystem>
#ifdef _WIN32
#  include <windows.h>
#endif

int main()
{
    // Set CWD to the directory containing EderHub.exe so relative paths work.
#ifdef _WIN32
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::filesystem::current_path(
        std::filesystem::path(exePath).parent_path());
#endif

    HubApp app;
    return app.Run();
}
