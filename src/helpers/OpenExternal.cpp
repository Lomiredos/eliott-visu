#include "visu/helpers/OpenExternal.hpp"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

void openInDefaultApp(const std::filesystem::path &_path)
{
    ShellExecuteW(nullptr, L"open", _path.wstring().c_str(),
                  nullptr, nullptr, SW_SHOWNORMAL);
}

void openInVSCode(const std::filesystem::path &_path)
{
    ShellExecuteW(nullptr, L"open", L"code", _path.wstring().c_str(),
                  nullptr, SW_SHOWNORMAL);
}

void launchProgram(const std::filesystem::path &_exePath)
{
    std::filesystem::path dir = _exePath.parent_path();
    ShellExecuteW(nullptr, L"open", _exePath.wstring().c_str(),
                  nullptr, dir.wstring().c_str(), SW_SHOWNORMAL);
}

#else

#include <unistd.h>
#include <sys/wait.h>

namespace
{
    // Double fork : le processus intermediaire sort immediatement, le
    // petit-fils (reparente a init) execute la commande. Le parent attend
    // seulement l'intermediaire, donc pas de zombie ni de blocage.
    void spawnDetached(const char *_workDir, const char *_file, char *const _argv[])
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            if (fork() == 0)
            {
                if (_workDir != nullptr)
                    (void)chdir(_workDir);
                execvp(_file, _argv);
                _exit(127);
            }
            _exit(0);
        }
        else if (pid > 0)
        {
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

void openInDefaultApp(const std::filesystem::path &_path)
{
    std::string path = _path.string();
    char *argv[] = {const_cast<char *>("xdg-open"), const_cast<char *>(path.c_str()), nullptr};
    spawnDetached(nullptr, "xdg-open", argv);
}

void openInVSCode(const std::filesystem::path &_path)
{
    std::string path = _path.string();
    char *argv[] = {const_cast<char *>("code"), const_cast<char *>(path.c_str()), nullptr};
    spawnDetached(nullptr, "code", argv);
}

void launchProgram(const std::filesystem::path &_exePath)
{
    std::string exe = _exePath.string();
    std::string dir = _exePath.parent_path().string();
    char *argv[] = {const_cast<char *>(exe.c_str()), nullptr};
    spawnDetached(dir.c_str(), exe.c_str(), argv);
}

#endif