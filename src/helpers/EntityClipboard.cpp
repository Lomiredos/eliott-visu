#include "visu/helpers/EntityClipboard.hpp"

#include "visu/core/SceneJson.hpp"
#include "visu/scene/SceneHierarchy.hpp"

#include <nlohmann/json.hpp>

#include <unordered_map>
#include <variant>
#include <vector>

namespace entityclipboard
{
    namespace
    {
        constexpr const char *kTag = "ee-visu/entities";
    }

    std::string copy(const SceneInfo &_scene, int _root, const std::string &_sceneKey)
    {
        const int n = static_cast<int>(_scene.entities.size());
        if (_root < 0 || _root >= n)
            return {};

        // Sous-arbre (racine + descendants), dans l'ordre de la scene.
        std::vector<int> parents = ee::scene::sceneParents(_scene);
        SceneInfo part;
        std::vector<int> ids;
        for (int i = 0; i < n; ++i)
            if (ee::scene::isInSubtree(parents, i, _root))
            {
                ids.push_back(i);
                part.entities.push_back(_scene.entities[i]);
            }

        // Place de la racine dans le monde : sert si son parent n'existe pas
        // la ou on colle.
        ee::scene::Xform w = ee::scene::sceneWorldXforms(_scene)[_root];

        nlohmann::json j;
        j["clipboard"] = kTag;
        j["scene"] = _sceneKey;
        j["root"] = _root;
        j["ids"] = ids; // index d'origine de chaque entite copiee
        j["entities"] = sceneToJson(part)["entities"];
        j["rootWorld"] = {w.pos.x, w.pos.y, w.pos.z, w.eulerDeg.x, w.eulerDeg.y, w.eulerDeg.z,
                          w.scale.x, w.scale.y, w.scale.z};
        return j.dump();
    }

    int paste(SceneInfo &_scene, const std::string &_text, const RefFields &_refFields,
              const std::string &_sceneKey)
    {
        nlohmann::json j = nlohmann::json::parse(_text, nullptr, false);
        if (!j.is_object() || j.value("clipboard", "") != kTag || !j.contains("entities"))
            return -1;

        nlohmann::json wrapper;
        wrapper["entities"] = j["entities"];
        SceneInfo part = sceneFromJson(wrapper);
        std::vector<int> ids = j.value("ids", std::vector<int>{});
        if (part.entities.empty() || ids.size() != part.entities.size())
            return -1;

        const bool sameScene = j.value("scene", "") == _sceneKey;
        const int oldCount = static_cast<int>(_scene.entities.size());
        const int root = j.value("root", -1);

        // Index d'origine -> index de la copie.
        std::unordered_map<int, int> remap;
        for (size_t k = 0; k < ids.size(); ++k)
            remap[ids[k]] = oldCount + static_cast<int>(k);

        int newRoot = -1;
        for (size_t k = 0; k < part.entities.size(); ++k)
        {
            EntityInfo &ent = part.entities[k];
            for (ComponentInstance &ci : ent.components)
            {
                auto comp = _refFields.find(ci.name);
                if (comp == _refFields.end())
                    continue;
                for (const auto &field : comp->second)
                {
                    auto it = ci.values.find(field.first);
                    if (it == ci.values.end())
                        continue;
                    int *v = std::get_if<int>(&it->second);
                    if (!v || *v < 0)
                        continue;
                    if (auto r = remap.find(*v); r != remap.end())
                        *v = r->second;
                    else if (!(sameScene && *v < oldCount))
                        *v = -1;
                }
            }
            if (ids[k] == root)
                newRoot = oldCount + static_cast<int>(k);
        }

        // Racine sans parent ici (autre scene) : on la pose a sa place monde.
        if (newRoot >= 0)
        {
            EntityInfo &r = part.entities[newRoot - oldCount];
            r.name = (r.name.empty() ? "Entity" : r.name) + " (copie)";
            ComponentInstance *tf = ee::scene::findTransform(r);
            bool hasParent = false;
            if (tf)
                if (auto it = tf->values.find("Parent"); it != tf->values.end())
                    if (const int *p = std::get_if<int>(&it->second))
                        hasParent = *p >= 0;
            std::vector<float> w = j.value("rootWorld", std::vector<float>{});
            if (tf && !hasParent && !sameScene && w.size() == 9)
                ee::scene::writeLocalXform(*tf, ee::scene::makeXform({w[0], w[1], w[2]}, {w[3], w[4], w[5]},
                                                                     {w[6], w[7], w[8]}));
        }

        for (EntityInfo &e : part.entities)
            _scene.entities.push_back(std::move(e));
        return newRoot;
    }
}
