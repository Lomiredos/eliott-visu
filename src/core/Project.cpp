#include "visu/core/Project.hpp"

namespace fs = std::filesystem;

namespace
{
    // Chemin absolu, sans "..", ni separateur final : name() serait vide avec
    // "mygame/", et le jeu (qui fait chdir avant execve) ne retrouverait pas
    // un exe relatif.
    fs::path normalizeRoot(const fs::path &_root)
    {
        std::error_code ec;
        fs::path p = fs::absolute(_root, ec);
        if (ec)
            p = _root;
        p = p.lexically_normal();
        if (!p.has_filename() && p.has_parent_path() && p != p.root_path())
            p = p.parent_path();
        return p;
    }
} // namespace

Project::Project(const fs::path &_root) : m_root(normalizeRoot(_root)) {}

bool Project::isValid() const
{
    return fs::is_directory(m_root);
}

std::string Project::name() const
{
    return m_root.filename().string();
}

fs::path Project::systemsDir() const
{
    return m_root / "Systems";
}

fs::path Project::componentsDir() const
{
    return m_root / "Components";
}

fs::path Project::engineIncludeDir() const
{
    return m_root / "extern" / "eliott-engine-3d" / "include" / "visu";
}

fs::path Project::componentsCatalog() const
{
    return m_root / "Assets" / "Components.json";
}

fs::path Project::scenesDataDir() const
{
    return m_root / "Assets/ScenesDatas";
}

fs::path Project::sceneFile(const std::string &_sceneName) const
{
    return scenesDataDir() / (_sceneName + ".json");
}

fs::path Project::executablePath() const
{
#ifdef _WIN32
    return m_root / "build" / (name() + ".exe");
#else
    return m_root / "build" / name();
#endif
}
