#include "visu/ui/CodeDock.hpp"

#include "visu/App.hpp"
#include "visu/ui/ShowCodePanel.hpp"

#include "imgui.h"

#include <vector>

namespace fs = std::filesystem;

void CodeDock::draw()
{
    m_dockId = ImGui::GetID(m_id.c_str());
    ImGui::DockSpace(m_dockId, ImVec2(0.0f, 0.0f));

    // .cpp en attente : on le place a droite du lecteur du .hpp des que
    // celui-ci a un noeud de dock (il lui faut une frame pour etre dessine).
    if (m_pendingCpp.empty())
        return;
    for (ShowCodePanel *p : App::getInstance().getPanels<ShowCodePanel>())
        if (p->isDockedIn(m_dockId) && p->path() == m_pendingFor && p->dockNode() != 0)
        {
            std::unique_ptr<ShowCodePanel> cpp = std::make_unique<ShowCodePanel>(m_pendingCpp);
            DockRequest &dock = cpp->dockRequest();
            dock.splitSource = p->dockNode();
            dock.splitDir = Dir::Right;
            dock.splitRatio = 0.5f;
            App::getInstance().requestPanel(std::move(cpp));
            m_pendingCpp.clear();
            m_pendingFor.clear();
            return;
        }
}

void CodeDock::keepAlive()
{
    m_dockId = ImGui::GetID(m_id.c_str());
    ImGui::DockSpace(m_dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_KeepAliveOnly);
}

void CodeDock::open(const fs::path &_path)
{
    App &app = App::getInstance();

    // Un .hpp qui a son .cpp a cote : on montre les deux, cote a cote.
    fs::path cpp = _path;
    cpp.replace_extension(".cpp");
    std::error_code ec;
    bool withCpp = _path.extension() == ".hpp" && fs::exists(cpp, ec);

    // Lecteurs reutilisables : dockes ici et non verrouilles.
    std::vector<ShowCodePanel *> free;
    for (ShowCodePanel *p : app.getPanels<ShowCodePanel>())
        if (p->isDockedIn(m_dockId) && !p->isLocked())
            free.push_back(p);

    auto take = [&free](bool _wantCpp) -> ShowCodePanel *
    {
        // Prefere un lecteur qui montrait deja le meme genre de fichier.
        for (size_t i = 0; i < free.size(); ++i)
            if ((free[i]->path().extension() == ".cpp") == _wantCpp)
            {
                ShowCodePanel *p = free[i];
                free.erase(free.begin() + i);
                return p;
            }
        if (free.empty())
            return nullptr;
        ShowCodePanel *p = free.front();
        free.erase(free.begin());
        return p;
    };

    m_pendingCpp.clear();
    m_pendingFor.clear();

    if (ShowCodePanel *main = take(false))
        main->changePath(_path);
    else
    {
        std::unique_ptr<ShowCodePanel> panel = std::make_unique<ShowCodePanel>(_path);
        panel->dockRequest().dockTarget = m_dockId;
        app.requestPanel(std::move(panel));
    }

    if (withCpp)
    {
        if (ShowCodePanel *side = take(true))
            side->changePath(cpp);
        else
        {
            m_pendingCpp = cpp; // cree au prochain draw(), a droite du .hpp
            m_pendingFor = _path;
        }
    }
    else
    {
        // Pas de .cpp (header-only) : un ancien lecteur .cpp ne doit pas
        // rester affiche avec le code d'un autre systeme.
        for (ShowCodePanel *p : free)
            if (p->path().extension() == ".cpp")
                p->setVisible(false);
    }
}
