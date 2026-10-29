#include "visu/helpers/GenerateCatalog.hpp"

namespace fs = std::filesystem;

namespace catalog
{
    bool start(projectbuild::AsyncBuild &_build, const fs::path &_projectRoot)
    {
#ifdef _WIN32
        fs::path genExe = _projectRoot / "build" / "gen_components.exe";
#else
        fs::path genExe = _projectRoot / "build" / "gen_components";
#endif
        // Le generateur est relance explicitement : son POST_BUILD ne tourne
        // pas si rien n'a ete recompile -> on garantit l'ecriture du json.
        return _build.start(_projectRoot, "gen_components", genExe);
    }
}
