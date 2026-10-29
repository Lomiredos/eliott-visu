#include "visu/helpers/OpenExternal.hpp"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

void openInVSCode(const std::filesystem::path &_file, const std::filesystem::path &_workspace)
{
    // lpParameters est une ligne de commande : sans guillemets, un chemin
    // avec espaces serait coupe en plusieurs arguments.
    const std::wstring args = L"\"" + _workspace.wstring() + L"\" -g \"" + _file.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"code", args.c_str(), nullptr, SW_SHOWNORMAL);
}

#else

#include <unistd.h>
#include <sys/wait.h>

namespace
{
    // Double fork : le processus intermediaire sort immediatement, le
    // petit-fils (reparente a init) execute la commande. Le parent attend
    // seulement l'intermediaire, donc pas de zombie ni de blocage.
    void spawnDetached(const char *_file, char *const _argv[])
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            if (fork() == 0)
            {
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

void openInVSCode(const std::filesystem::path &_file, const std::filesystem::path &_workspace)
{
    std::string workspace = _workspace.string();
    std::string file = _file.string();
    char *argv[] = {const_cast<char *>("code"), workspace.data(), const_cast<char *>("-g"),
                    file.data(), nullptr};
    spawnDetached("code", argv);
}

#endif