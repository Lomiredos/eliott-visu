#pragma once

#include <filesystem>
#include <memory>
#include <string>

// ---------------------------------------------------------------------------
// Compile le PROJET courant (cible par defaut) via cmake, en capturant la
// sortie du compilo pour l'afficher en cas d'echec. Referme la boucle
// authoring -> build -> Play sans quitter l'editeur.
// Tourne dans un thread : l'UI continue de s'afficher et montre la
// progression (les "[ 42%]" de make, "[12/50]" de ninja).
// ---------------------------------------------------------------------------

namespace projectbuild
{
    struct BuildResult
    {
        bool ok = false;
        std::string log; // sortie complete (a montrer si !ok)
    };

    // Le dernier build reussi (build/.ee-build-stamp, date = son debut) est
    // plus recent que tous les fichiers du projet (hors build*/, Assets/,
    // fichiers caches) et que le cache cmake : rien a recompiler, Play peut le
    // lancer sans passer par cmake (qui coute plusieurs secondes sous Windows
    // meme sans rien a faire).
    bool upToDate(const std::filesystem::path &_projectRoot, const std::filesystem::path &_exe);

    class AsyncBuild
    {
    public:
        AsyncBuild();

        // _target vide = cible par defaut (le jeu). _then : executable lance
        // apres un build reussi (sa sortie s'ajoute au log, son code decide ok).
        // false si un build tourne deja (celui-ci ou un autre : un seul cmake a
        // la fois dans build/).
        bool start(const std::filesystem::path &_projectRoot, const std::string &_target = "",
                   const std::filesystem::path &_then = {});
        bool running() const;

        // Vrai une seule fois, quand le build vient de finir (resultat dans _out).
        bool finished(BuildResult &_out);

        // Un build (n'importe lequel) tourne dans l'editeur.
        static bool anyRunning();

        int percent() const;      // -1 tant qu'inconnu (configuration cmake...)
        std::string step() const; // fichier en cours ("main.cpp", "Edition des liens")

    private:
        struct Shared;
        // Partage avec le thread (detache) : survit a l'editeur s'il quitte
        // pendant un build.
        std::shared_ptr<Shared> m_shared;
    };
}
