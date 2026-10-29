#pragma once

#include <filesystem>
#include <memory>
#include <string>

// ---------------------------------------------------------------------------
// Mise a jour du moteur (extern/eliott-engine-3d) au lancement de l'editeur :
// git fetch, commits en avance sur origin -> l'editeur propose un pull.
// Tout tourne dans un thread (le reseau peut etre lent) : l'UI interroge
// state() a chaque frame. Hors ligne / pas un depot git : on ne dit rien.
// ---------------------------------------------------------------------------
namespace engineupdate
{
    enum class State
    {
        Idle,
        Checking,
        UpdateAvailable, // commits() : les nouveaux commits (git log --oneline)
        UpToDate,        // ou verification impossible (hors ligne...)
        Pulling,
        Pulled,
        PullFailed // log() : sortie de git
    };

    class Checker
    {
    public:
        explicit Checker(std::filesystem::path _engineDir);

        void startCheck(); // Idle -> Checking -> UpdateAvailable | UpToDate
        void startPull();  // UpdateAvailable -> Pulling -> Pulled | PullFailed
        void dismiss();    // retour a Idle (popup traitee)

        State state() const;
        std::string commits() const;
        std::string log() const;

    private:
        struct Shared;
        std::filesystem::path m_dir;
        // Partage avec le thread (detache) : survit a l'editeur s'il quitte
        // pendant un fetch.
        std::shared_ptr<Shared> m_shared;
    };
}
