#pragma once

#include "visu/helpers/BuildProject.hpp"

#include <filesystem>

// ---------------------------------------------------------------------------
// Genere Components.json du projet par VRAIE compilation :
//   1. compile la cible gen_components (inclut les structs de composants) ;
//      si une struct est invalide, la compilation echoue -> c'est le "test".
//   2. si OK, lance gen_components -> ecrit Assets/Components.json.
// Dans le thread de _build (barre de progression, resultat via finished()).
// gen_components ne compile que la reflexion (ee-reflect), pas le moteur.
// ---------------------------------------------------------------------------

namespace catalog
{
    // false si un build tourne deja.
    bool start(projectbuild::AsyncBuild &_build, const std::filesystem::path &_projectRoot);
}
