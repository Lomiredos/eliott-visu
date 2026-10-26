#include "visu/core/GameProcess.hpp"

#include "visu/core/SceneJson.hpp"

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace fs = std::filesystem;

namespace
{
    // Nom unique du bloc de memoire partagee (plusieurs editeurs possibles).
    std::string uniqueShmName()
    {
        static int s_counter = 0;
#ifdef _WIN32
        unsigned long pid = GetCurrentProcessId();
#else
        long pid = static_cast<long>(getpid());
#endif
        return "ee_visu_" + std::to_string(pid) + "_" + std::to_string(s_counter++);
    }

    void sleepMs(int _ms) { std::this_thread::sleep_for(std::chrono::milliseconds(_ms)); }
}

GameProcess::~GameProcess()
{
    stop(); // jamais de jeu orphelin
}

bool GameProcess::start(const fs::path &_exe, const fs::path &_logPath,
                        ee::ipc::PlayMode _mode, std::string &_error)
{
    stop();
    acknowledgeCrash();

    std::error_code ec;
    if (!fs::is_regular_file(_exe, ec))
    {
        _error = "Executable introuvable : " + _exe.string() + " (build le projet).";
        return false;
    }

    m_mode = _mode;
    m_logPath = _logPath;

    uint16_t port = 0;
    m_listen = ee::ipc::Socket::listen(port);
    if (!m_listen.valid())
    {
        _error = "Impossible d'ouvrir la socket locale.";
        return false;
    }

    ee::ipc::HostInfo host;
    host.port = port;
    host.mode = _mode;
    if (_mode == ee::ipc::PlayMode::Preview)
    {
        host.shmName = uniqueShmName();
        if (!m_frame.create(host.shmName))
        {
            _error = "Impossible de creer la memoire partagee.";
            cleanup();
            return false;
        }
    }
    const std::string hostVar = std::string(ee::ipc::kEditorHostEnv) + "=" + ee::ipc::formatHostInfo(host);

#ifdef _WIN32
    // Environnement = celui de l'editeur + EE_EDITOR_HOST.
    std::wstring hostVarW(hostVar.begin(), hostVar.end());
    std::wstring prefixW = std::wstring(hostVarW.begin(), hostVarW.begin() + hostVarW.find(L'=') + 1);
    std::vector<wchar_t> env;
    if (wchar_t *block = GetEnvironmentStringsW())
    {
        for (const wchar_t *p = block; *p; p += wcslen(p) + 1)
            if (wcsncmp(p, prefixW.c_str(), prefixW.size()) != 0)
                env.insert(env.end(), p, p + wcslen(p) + 1);
        FreeEnvironmentStringsW(block);
    }
    env.insert(env.end(), hostVarW.begin(), hostVarW.end());
    env.push_back(L'\0');
    env.push_back(L'\0');

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE}; // le jeu herite du log
    HANDLE logHandle = CreateFileW(_logPath.wstring().c_str(), GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (logHandle != INVALID_HANDLE_VALUE)
    {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = logHandle;
        si.hStdError = logHandle;
    }
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + _exe.wstring() + L"\"";
    std::wstring dir = _exe.parent_path().wstring();
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env.data(),
                             dir.c_str(), &si, &pi);
    if (logHandle != INVALID_HANDLE_VALUE)
        CloseHandle(logHandle);
    if (!ok)
    {
        _error = "Lancement du jeu impossible (CreateProcess, erreur " + std::to_string(GetLastError()) + ").";
        cleanup();
        return false;
    }
    CloseHandle(pi.hThread);
    m_process = pi.hProcess;
#else
    // Tout est prepare AVANT le fork : l'enfant n'appelle que des fonctions
    // sures (chdir, open, dup2, execve).
    std::string exe = _exe.string();
    std::string dir = _exe.parent_path().string();
    std::string logPath = _logPath.string();
    const std::string prefix = std::string(ee::ipc::kEditorHostEnv) + "=";
    std::vector<char *> envp;
    for (char **e = environ; *e; ++e)
        if (std::strncmp(*e, prefix.c_str(), prefix.size()) != 0)
            envp.push_back(*e);
    envp.push_back(const_cast<char *>(hostVar.c_str()));
    envp.push_back(nullptr);
    char *argv[] = {exe.data(), nullptr};

    pid_t pid = fork();
    if (pid < 0)
    {
        _error = "Lancement du jeu impossible (fork).";
        cleanup();
        return false;
    }
    if (pid == 0)
    {
        if (chdir(dir.c_str()) != 0)
            _exit(126);
        int fd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0)
        {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO)
            {
                close(fd);
            }
        }
        execve(exe.c_str(), argv, envp.data());
        _exit(127); // executable introuvable / non executable
    }
    m_pid = pid;
#endif

    m_state = State::Starting;
    m_stopRequested = false;
    m_startTime = std::chrono::steady_clock::now();
    return true;
}

void GameProcess::update()
{
    if (!running())
        return;

    if (m_state == State::Starting)
    {
        ee::ipc::Socket c = m_listen.accept();
        if (c.valid())
        {
            m_conn = std::move(c);
            m_listen.close(); // une seule connexion par session
            m_state = State::Running;
            m_sentW = m_sentH = 0;
        }
    }

    if (m_state == State::Running)
    {
        std::vector<nlohmann::json> msgs;
        m_conn.poll(msgs);
        for (const nlohmann::json &m : msgs)
            if (ee::ipc::msgType(m) == "snap" && m.contains("scene"))
            {
                m_snapshot = sceneFromJson(m["scene"]);
                ++m_snapshotVersion;
            }
    }

    if (m_mode == ee::ipc::PlayMode::Preview)
        uploadFrame();

    bool clean = false;
    std::string how;
    if (processExited(clean, how))
    {
        // Sortie normale (Stop, fenetre du jeu fermee) : retour a l'edition.
        // Sinon : crash, l'editeur reste vivant et affiche le log.
        if (clean || m_stopRequested)
            m_state = State::Idle;
        else
        {
            m_crashMessage = how;
            m_state = State::Crashed;
        }
        cleanup();
    }
}

void GameProcess::stop()
{
    if (!running())
        return;
    m_stopRequested = true;
    m_conn.send(ee::ipc::msgStop());

    // Laisse ~1 s au jeu pour sortir proprement, sinon on le tue.
    bool clean = false;
    std::string how;
    bool exited = false;
    for (int i = 0; i < 100 && !exited; ++i)
    {
        std::vector<nlohmann::json> ignored;
        m_conn.poll(ignored); // envoie le Stop s'il etait encore en attente
        exited = processExited(clean, how);
        if (!exited)
            sleepMs(10);
    }
    if (!exited)
        kill();

    m_state = State::Idle;
    cleanup();
}

void GameProcess::kill()
{
#ifdef _WIN32
    if (m_process)
    {
        TerminateProcess(static_cast<HANDLE>(m_process), 1);
        WaitForSingleObject(static_cast<HANDLE>(m_process), 2000);
        CloseHandle(static_cast<HANDLE>(m_process));
        m_process = nullptr;
    }
#else
    if (m_pid > 0)
    {
        ::kill(m_pid, SIGKILL);
        int status = 0;
        waitpid(m_pid, &status, 0);
        m_pid = -1;
    }
#endif
}

bool GameProcess::processExited(bool &_clean, std::string &_how)
{
#ifdef _WIN32
    if (!m_process)
        return true;
    if (WaitForSingleObject(static_cast<HANDLE>(m_process), 0) != WAIT_OBJECT_0)
        return false;
    DWORD code = 0;
    GetExitCodeProcess(static_cast<HANDLE>(m_process), &code);
    CloseHandle(static_cast<HANDLE>(m_process));
    m_process = nullptr;

    _clean = (code == 0);
    char buf[64];
    if (code >= 0xC0000000u) // exception Windows (ex. 0xC0000005 = acces memoire invalide)
        std::snprintf(buf, sizeof(buf), "exception 0x%08lX", static_cast<unsigned long>(code));
    else
        std::snprintf(buf, sizeof(buf), "code de sortie %lu", static_cast<unsigned long>(code));
    _how = std::string("Le jeu s'est arrete : ") + buf;
    return true;
#else
    if (m_pid <= 0)
        return true;
    int status = 0;
    pid_t r = waitpid(m_pid, &status, WNOHANG);
    if (r == 0)
        return false; // toujours vivant
    m_pid = -1;

    if (r < 0)
    {
        _clean = false;
        _how = "Le jeu s'est arrete (etat inconnu).";
    }
    else if (WIFEXITED(status))
    {
        int code = WEXITSTATUS(status);
        _clean = (code == 0);
        if (code == 127)
            _how = "Le jeu n'a pas pu etre lance (executable introuvable ou non executable).";
        else if (code == 126)
            _how = "Le jeu n'a pas pu etre lance (dossier de l'executable inaccessible).";
        else
            _how = "Le jeu s'est arrete : code de sortie " + std::to_string(code);
    }
    else if (WIFSIGNALED(status))
    {
        int sig = WTERMSIG(status);
        _clean = false;
        const char *name = strsignal(sig);
        _how = "Le jeu a plante : signal " + std::to_string(sig) + " (" + (name ? name : "?") + ")";
    }
    else
    {
        _clean = false;
        _how = "Le jeu s'est arrete.";
    }
    return true;
#endif
}

void GameProcess::cleanup()
{
    m_conn.close();
    m_listen.close();
    m_frame.close();
    m_frameCounter = 0;
    m_hasFrame = false;
    m_snapshot.reset();
    m_sentW = m_sentH = 0;
    m_stopRequested = false;
#ifdef _WIN32
    if (m_process)
    {
        CloseHandle(static_cast<HANDLE>(m_process));
        m_process = nullptr;
    }
#endif
}

void GameProcess::uploadFrame()
{
    const uint8_t *px = nullptr;
    int w = 0, h = 0;
    if (!m_frame.latest(m_frameCounter, px, w, h))
        return;

    static bool s_glLoaded = gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress));
    if (!s_glLoaded)
        return;

    if (m_texture == 0)
    {
        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else
        glBindTexture(GL_TEXTURE_2D, m_texture);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (w != m_texW || h != m_texH)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        m_texW = w;
        m_texH = h;
    }
    else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_hasFrame = true;
}

void GameProcess::releaseGl()
{
    if (m_texture != 0)
    {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
        m_texW = m_texH = 0;
    }
    m_hasFrame = false;
}

void GameProcess::sendResize(int _w, int _h)
{
    if (m_state != State::Running || _w <= 0 || _h <= 0 || (_w == m_sentW && _h == m_sentH))
        return;
    m_conn.send(ee::ipc::msgResize(_w, _h));
    m_sentW = _w;
    m_sentH = _h;
}

void GameProcess::sendInput(const ee::ipc::InputState &_in)
{
    if (m_state == State::Running)
        m_conn.send(ee::ipc::msgInput(_in));
}

void GameProcess::sendSetField(int _entity, const std::string &_comp, const std::string &_field,
                               const FieldValue &_value)
{
    if (m_state == State::Running)
        m_conn.send(ee::ipc::msgSetField(_entity, _comp, _field, _value));
}

std::string GameProcess::log() const
{
    std::ifstream f(m_logPath, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    const size_t kMax = 64 * 1024; // garde la fin : c'est la qu'est le crash
    if (s.size() > kMax)
        s = "[...]\n" + s.substr(s.size() - kMax);
    return s;
}

void GameProcess::acknowledgeCrash()
{
    if (m_state == State::Crashed)
        m_state = State::Idle;
    m_crashMessage.clear();
}
