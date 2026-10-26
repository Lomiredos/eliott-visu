#pragma once

#include "visu/core/SceneInfo.hpp"

#include <map>
#include <string>

// ---------------------------------------------------------------------------
// Copier / coller d'entites (Ctrl+C / Ctrl+V).
//
// Copier prend l'entite ET ses enfants (sous-arbre), en texte JSON pose dans
// le presse-papiers du systeme : on peut coller dans une autre scene, ou dans
// un autre editeur. Les champs "entity" (_refFields, ex. Transform.Parent,
// PlayerComponent.Camera) sont recables au collage :
//   - vers une entite copiee -> vers sa copie ;
//   - vers une autre entite -> gardee si on colle dans la meme scene, sinon
//     videe (-1).
// La copie est collee a cote de l'original (meme parent), a la meme place
// dans le monde ; dans une autre scene, a la racine.
// ---------------------------------------------------------------------------
namespace entityclipboard
{
    // Champs "entity" par composant : { "TransformComponent": { "Parent": ... } }.
    using RefFields = std::map<std::string, std::map<std::string, std::string>>;

    // Texte a mettre dans le presse-papiers ("" si _root invalide).
    // _sceneKey identifie la scene (projet + nom) pour savoir au collage si
    // on est dans la meme.
    std::string copy(const SceneInfo &_scene, int _root, const std::string &_sceneKey);

    // Colle a la fin de _scene. Renvoie l'index de la copie de la racine, ou
    // -1 si _text n'est pas une copie d'entites.
    int paste(SceneInfo &_scene, const std::string &_text, const RefFields &_refFields,
              const std::string &_sceneKey);
}
