#include "visu/helpers/BuildProject.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace
{
    std::atomic<bool> g_anyRunning{false};

    // Date = debut du dernier build reussi lance par l'editeur.
    fs::path stampPath(const fs::path &_projectRoot)
    {
        return _projectRoot / "build" / ".ee-build-stamp";
    }

    // "[ 42%] Building..." (make) ou "[12/50] Building..." (ninja) -> 0..100.
    int parsePercent(const std::string &_line)
    {
        int a = 0, b = 0, n = 0;
        // %n : sscanf renvoie 1 des que %d est lu, meme si le '%' qui suit
        // ne correspond pas ("[12/50]") ; n n'est ecrit que si tout a matche.
        if (std::sscanf(_line.c_str(), "[%d%%]%n", &a, &n) == 1 && n > 0)
            return std::clamp(a, 0, 100);
        if (std::sscanf(_line.c_str(), "[%d/%d]", &a, &b) == 2 && b > 0)
            return std::clamp(a * 100 / b, 0, 100);
        return -1;
    }

    // Une etape de make/ninja (compilation ou edition des liens).
    bool isStep(const std::string &_line)
    {
        return _line.find("Building") != std::string::npos ||
               _line.find("Linking") != std::string::npos;
    }

    // Lance _cmd, appelle _onLine pour chaque ligne (sans \n). Code de retour.
    template <typename F>
    int runLines(const std::string &_cmd, F &&_onLine, std::string *_log = nullptr)
    {
#ifdef _WIN32
        FILE *pipe = _popen(_cmd.c_str(), "r");
#else
        FILE *pipe = popen(_cmd.c_str(), "r");
#endif
        if (!pipe)
            return -1;
        char line[1024];
        while (std::fgets(line, sizeof(line), pipe))
        {
            if (_log)
                *_log += line;
            std::string l = line;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
                l.pop_back();
            _onLine(l);
        }
#ifdef _WIN32
        return _pclose(pipe);
#else
        return pclose(pipe);
#endif
    }

    // "Building CXX object CMakeFiles/jeu.dir/src/main.cpp.o" -> "main.cpp".
    std::string parseStep(const std::string &_line)
    {
        if (_line.find("Linking") != std::string::npos)
            return "Edition des liens";
        if (_line.find("Building") == std::string::npos)
            return "";
        std::string obj = _line.substr(_line.find_last_of(' ') + 1);
        obj = obj.substr(obj.find_last_of("/\\") + 1);
        for (const char *ext : {".obj", ".o"})
        {
            size_t n = std::char_traits<char>::length(ext);
            if (obj.size() > n && obj.compare(obj.size() - n, n, ext) == 0)
            {
                obj.resize(obj.size() - n);
                break;
            }
        }
        return obj;
    }
}

namespace projectbuild
{
    bool upToDate(const fs::path &_projectRoot, const fs::path &_exe)
    {
        std::error_code ec;
        if (!fs::exists(_exe, ec))
            return false; // pas encore compile
        // Debut du build, pas date de l'exe : un fichier sauvegarde PENDANT le
        // build (compile dans son ancienne version) est plus recent que lui.
        // Pas de stamp (compile hors de l'editeur) : on passe par cmake.
        const fs::file_time_type built = fs::last_write_time(stampPath(_projectRoot), ec);
        if (ec)
            return false;

        // Tout le projet (code du jeu, moteur dans extern/, CMakeLists, dossiers
        // ajoutes a la main...) sauf ce qui n'est jamais compile.
        fs::recursive_directory_iterator it(_projectRoot, ec), end;
        for (; it != end && !ec; it.increment(ec))
        {
            const std::string name = it->path().filename().string();
            // Caches : .git (dossier, ou fichier dans un sous-module), .cache/
            // de clangd (reecrit en permanence), .vscode/... : pas des sources.
            if (!name.empty() && name[0] == '.')
            {
                it.disable_recursion_pending();
                continue;
            }
            std::error_code dirEc; // lien casse : on le saute, pas tout le parcours
            if (it->is_directory(dirEc))
            {
                // build*/ a la racine, ou tout dossier de build cmake (avec
                // CMakeCache.txt) : sorties de compilation (le cache cmake est
                // vu a part). Pas un simple prefixe a toute profondeur : un
                // dossier de sources "buildings/" doit rester surveille.
                // Assets/ a la racine : lu par le jeu au lancement, et le Play
                // y sauvegarde la scene a chaque fois.
                std::error_code cacheEc;
                if ((it.depth() == 0 && (name.rfind("build", 0) == 0 || name == "Assets")) ||
                    fs::exists(it->path() / "CMakeCache.txt", cacheEc))
                    it.disable_recursion_pending();
                continue;
            }
            std::error_code e;
            auto t = fs::last_write_time(it->path(), e);
            if (!e && t > built)
                return false;
        }
        if (ec)
            return false; // parcours interrompu : dans le doute, on compile

        // Reconfiguration cmake (cache modifie) : il faut passer par cmake.
        auto cache = fs::last_write_time(_projectRoot / "build" / "CMakeCache.txt", ec);
        if (!ec && cache > built)
            return false;
        return true;
    }

    struct AsyncBuild::Shared
    {
        std::atomic<bool> running{false};
        std::atomic<bool> done{false}; // fini, pas encore lu par finished()
        std::atomic<int> percent{-1};
        mutable std::mutex mutex; // step, result
        std::string step;
        BuildResult result;
    };

    AsyncBuild::AsyncBuild() : m_shared(std::make_shared<Shared>()) {}

    bool AsyncBuild::start(const fs::path &_projectRoot, const std::string &_target,
                           const fs::path &_then)
    {
        bool expected = false;
        if (!g_anyRunning.compare_exchange_strong(expected, true))
            return false;
        m_shared->running = true;
        m_shared->done = false;
        m_shared->percent = -1;
        {
            std::lock_guard<std::mutex> lock(m_shared->mutex);
            m_shared->step.clear();
            m_shared->result = BuildResult{};
        }

        // Chemin natif quote -> les espaces ne cassent pas la commande.
        // Cible par defaut (tout), config Debug (convention du projet edite).
        fs::path build = (_projectRoot / "build").make_preferred();
        // Le stamp ne date que les builds du jeu (cible par defaut).
        const fs::path stamp = _target.empty() ? stampPath(_projectRoot) : fs::path();
        const fs::file_time_type startTime = fs::file_time_type::clock::now();
        unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
        std::string cmd = "cmake --build \"" + build.string() + "\" --config Debug -j" +
                          std::to_string(jobs);
        if (!_target.empty())
            cmd += " --target " + _target;
        std::string dry = _target.empty() ? "" : cmd + " -- -n 2>&1";
        cmd += " 2>&1";
        std::string then =
            _then.empty() ? "" : "\"" + fs::path(_then).make_preferred().string() + "\" 2>&1";
#ifdef _WIN32
        // cmd /c retire la 1re et la derniere " d'une commande qui commence par
        // " (chemin avec parentheses...) : une paire en plus pour lui.
        if (!then.empty())
            then = "\"" + then + "\"";
#endif

        std::thread([cmd, dry, then, stamp, startTime, s = m_shared]()
                    {
                        std::string log;
                        // Cible hors de "all" (gen_components) : make n'affiche
                        // pas de "[ 42%]". On compte les etapes, annoncees par
                        // une passe a blanc (-n, ~20 ms) ; +1 : la derniere
                        // edition des liens n'y figure pas.
                        int total = 0;
                        if (!dry.empty())
                            runLines(dry, [&](const std::string &_l)
                                     { total += isStep(_l) ? 1 : 0; });
                        int done = 0;
                        bool sawPercent = false;
                        int rc = runLines(cmd, [&](const std::string &_l)
                                          {
                                              int p = parsePercent(_l);
                                              if (p >= 0)
                                              {
                                                  sawPercent = true;
                                                  s->percent = std::max(s->percent.load(), p);
                                              }
                                              if (isStep(_l) && !sawPercent && total > 0)
                                                  s->percent = std::min(99, ++done * 100 / (total + 1));
                                              std::string step = parseStep(_l);
                                              if (!step.empty())
                                              {
                                                  std::lock_guard<std::mutex> lock(s->mutex);
                                                  s->step = step;
                                              }
                                          },
                                          &log);
                        if (rc == -1 && log.empty())
                            log = "Impossible de lancer cmake.";
                        if (rc == 0 && !then.empty())
                        {
                            {
                                std::lock_guard<std::mutex> lock(s->mutex);
                                s->step = "Generation";
                            }
                            rc = runLines(then, [](const std::string &) {}, &log);
                        }
                        if (rc == 0 && !stamp.empty())
                        {
                            std::error_code ec;
                            std::ofstream(stamp).close();
                            fs::last_write_time(stamp, startTime, ec);
                            if (ec)
                                fs::remove(stamp, ec); // sans date fiable, pas de stamp
                        }
                        {
                            std::lock_guard<std::mutex> lock(s->mutex);
                            s->result.ok = (rc == 0);
                            s->result.log = std::move(log);
                        }
                        s->done = true;
                        s->running = false;
                        g_anyRunning = false;
                    })
            .detach();
        return true;
    }

    bool AsyncBuild::running() const { return m_shared->running; }

    bool AsyncBuild::anyRunning() { return g_anyRunning; }

    bool AsyncBuild::finished(BuildResult &_out)
    {
        if (!m_shared->done.exchange(false))
            return false;
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        _out = m_shared->result;
        return true;
    }

    int AsyncBuild::percent() const { return m_shared->percent; }

    std::string AsyncBuild::step() const
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        return m_shared->step;
    }
}
