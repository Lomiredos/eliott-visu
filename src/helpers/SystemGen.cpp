#include "visu/helpers/SystemGen.hpp"

#include "visu/helpers/NameValidation.hpp"
#include "visu/core/SystemInfo.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    // "TransformComponent" -> "transform" : nom de variable lisible pour les
    // exemples d'acces typé dans le stub.
    std::string varNameFor(const std::string &_comp)
    {
        std::string s = _comp;
        const std::string suffix = "Component";
        if (s.size() > suffix.size() &&
            s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
            s = s.substr(0, s.size() - suffix.size());
        if (!s.empty())
            s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
        return s.empty() ? "c" : s;
    }

    bool writeFile(const fs::path &_path, const std::string &_content)
    {
        std::ofstream f(_path, std::ios::binary);
        if (!f)
            return false; // fichier verrouille / lecture seule / chemin invalide
        f << _content;
        return f.good();
    }

    void regenerateRegister(const fs::path &_systemsDir)
    {
        std::vector<std::string> names;
        std::error_code ec;
        for (fs::directory_iterator it(_systemsDir, ec), end; it != end && !ec; it.increment(ec))
        {
            const auto &entry = *it;
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;
            if (auto info = loadSystemInfo(entry.path()))
                if (!info->name.empty())
                    names.push_back(info->name);
        }

        writeFile(_systemsDir / "RegisterSystems.hpp",
                  "#pragma once\n\n"
                  "#include \"visu/scene/SystemHost.hpp\"\n\n"
                  "// Genere par EE-Visu. Associe chaque nom de systeme a son type C++.\n"
                  "void registerGameSystems(ee::scene::SystemHost &_host);\n");

        std::ostringstream cpp;
        cpp << "#include \"Systems/RegisterSystems.hpp\"\n\n";
        cpp << "// Genere par EE-Visu -- ne pas editer a la main.\n";
        for (const std::string &n : names)
            cpp << "#include \"Systems/" << n << ".hpp\"\n";
        cpp << "\nvoid registerGameSystems(ee::scene::SystemHost &_host)\n{\n";
        for (const std::string &n : names)
            cpp << "    _host.reg(\"" << n << "\", ee::scene::makeSystemFactory<" << n << ">());\n";
        cpp << "}\n";
        writeFile(_systemsDir / "RegisterSystems.cpp", cpp.str());
    }
}

namespace systemgen
{
    CreateResult createSystem(const fs::path &_systemsDir, const SystemInfoCreation &_def)
    {
        // Nom invalide -> refus (identifiant C++ + nom de fichier surs).
        if (!namevalidation::isValidIdentifier(_def.name))
            return {{}, "Nom invalide (identifiant C++ requis)."};

        const std::string &name = _def.name;

        // Collision : ne pas ecraser un systeme existant (dont son .cpp edite
        // a la main) en silence.
        fs::path jsonPath = _systemsDir / (name + ".json");
        fs::path hppPath = _systemsDir / (name + ".hpp");
        fs::path cppPath = _systemsDir / (name + ".cpp");
        if (fs::exists(jsonPath) || fs::exists(hppPath) || fs::exists(cppPath))
            return {{}, "\"" + name + "\" existe deja."};

        std::error_code ec;
        fs::create_directories(_systemsDir, ec);

        nlohmann::json j;
        j["name"] = name;
        j["components"] = _def.requiredComponentName;
        j["priority"] = 0;
        j["category"] = "gameplay";
        if (!writeFile(jsonPath, j.dump(2) + "\n"))
            return {{}, "Ecriture impossible (fichier verrouille ?)."};

        std::ostringstream hpp;
        hpp << "#pragma once\n\n"
            << "#include \"ecs/System.hpp\"\n\n"
            << "// Systeme genere par EE-Visu. Classe ee::ecs::UpdateSystem : update()\n"
            << "// itere m_entities -> uniquement les entites qui matchent la signature.\n"
            << "class " << name << " : public ee::ecs::UpdateSystem\n"
            << "{\n"
            << "public:\n"
            << "    void update(ee::ecs::World &_world, float _dt) override;\n"
            << "};\n";
        if (!writeFile(hppPath, hpp.str()))
            return {{}, "Ecriture impossible (fichier verrouille ?)."};

        std::ostringstream cpp;
        cpp << "#include \"Systems/" << name << ".hpp\"\n\n"
            << "#include \"ecs/World.hpp\"\n"
            << "#include \"input/InputManager.hpp\"\n"
            << "#include \"visu/components/Components.hpp\" // composants moteur (Transform, formes...)\n"
            << "// + #include \"Components/TonComposant.hpp\" pour tes composants de jeu\n\n"
            << "void " << name << "::update(ee::ecs::World &_world, float _dt)\n"
            << "{\n"
            << "    for (ee::ecs::EntityID e : m_entities)\n"
            << "    {\n"
            << "        // Acces typé aux composants de la signature :\n";
        for (const std::string &c : _def.requiredComponentName)
            cpp << "        // " << c << " &" << varNameFor(c)
                << " = *_world.getComponent<" << c << ">(e);\n";
        cpp << "        // Entree du frame : ee::input::InputManager::getInstance().IsKeyDown(ee::input::Key::W)\n"
            << "        // TODO: ta logique (ex. " << (_def.requiredComponentName.empty() ? "..." : varNameFor(_def.requiredComponentName.front()))
            << " ...).\n"
            << "        (void)_world; (void)_dt; (void)e;\n"
            << "    }\n"
            << "}\n";
        if (!writeFile(cppPath, cpp.str()))
            return {{}, "Ecriture impossible (fichier verrouille ?)."};

        regenerateRegister(_systemsDir);

        return {cppPath, {}};
    }
}
