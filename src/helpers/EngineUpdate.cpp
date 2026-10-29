#include "visu/helpers/EngineUpdate.hpp"

#include "visu/helpers/FileIO.hpp"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace engineupdate
{
    struct Checker::Shared
    {
        std::atomic<State> state{State::Idle};
        std::mutex mutex; // commits, log
        std::string commits;
        std::string log;
    };

    namespace
    {
        // git -C <dir> <args>, sortie (stdout + stderr) dans _out.
        bool git(const fs::path &_dir, const std::string &_args, std::string &_out)
        {
            std::error_code ec;
            fs::path log =
                fs::temp_directory_path(ec) /
                ("ee-visu-git-" +
                 std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + ".log");
            const std::string cmd = "git -C \"" + fs::path(_dir).make_preferred().string() + "\" " +
                                    _args + " > \"" + log.make_preferred().string() + "\" 2>&1";
            int rc = std::system(cmd.c_str());
            _out = fileio::readFile(log);
            fs::remove(log, ec);
            return rc == 0;
        }
    }

    Checker::Checker(fs::path _engineDir)
        : m_dir(std::move(_engineDir)), m_shared(std::make_shared<Shared>())
    {
    }

    void Checker::startCheck()
    {
        std::error_code ec;
        if (state() != State::Idle || !fs::exists(m_dir / ".git", ec))
            return; // editeur installe sans les sources du moteur : rien a faire

        // Jamais de demande de mot de passe dans le terminal (le thread bloquerait).
#ifdef _WIN32
        _putenv_s("GIT_TERMINAL_PROMPT", "0");
#else
        setenv("GIT_TERMINAL_PROMPT", "0", 1);
#endif
        m_shared->state = State::Checking;
        std::thread(
            [dir = m_dir, s = m_shared]()
            {
                std::string out;
                if (!git(dir, "fetch --quiet", out))
                {
                    s->state = State::UpToDate; // hors ligne, pas d'upstream...
                    return;
                }
                // Branche sans upstream (HEAD detache d'un sous-module) : echoue, on se tait.
                if (!git(dir, "log --oneline \"HEAD..@{u}\"", out) || out.empty())
                {
                    s->state = State::UpToDate;
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(s->mutex);
                    s->commits = out;
                }
                s->state = State::UpdateAvailable;
            })
            .detach();
    }

    void Checker::startPull()
    {
        if (state() != State::UpdateAvailable)
            return;
        m_shared->state = State::Pulling;
        std::thread(
            [dir = m_dir, s = m_shared]()
            {
                std::string out;
                // --ff-only : jamais de merge cree dans le dos de l'utilisateur.
                bool ok = git(dir, "pull --ff-only", out);
                {
                    std::lock_guard<std::mutex> lock(s->mutex);
                    s->log = out;
                }
                s->state = ok ? State::Pulled : State::PullFailed;
            })
            .detach();
    }

    void Checker::dismiss()
    {
        State st = state();
        if (st != State::Checking && st != State::Pulling)
            m_shared->state = State::Idle;
    }

    State Checker::state() const
    {
        return m_shared->state;
    }

    std::string Checker::commits() const
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        return m_shared->commits;
    }

    std::string Checker::log() const
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        return m_shared->log;
    }
}
