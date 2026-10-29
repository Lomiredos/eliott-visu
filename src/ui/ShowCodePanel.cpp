#include "visu/ui/ShowCodePanel.hpp"

#include "visu/ui/FileReader.hpp"
#include "imgui.h"
#include "imgui_internal.h" // DockNodeGetRootNode
#include "IconsFontAwesome6.h"

ShowCodePanel::ShowCodePanel(fs::path _path)
{
    m_editor.SetReadOnlyEnabled(true);
    m_editor.SetPalette(TextEditor::PaletteId::Dark);

    static int s_counter = 0;
    m_idSuffix = "###showcode_" + std::to_string(s_counter++);
    removeOnClose = true;

    changePath(_path);
}

void ShowCodePanel::draw(Project *project)
{
    m_dockNode = ImGui::GetWindowDockID();
    ImGuiDockNode *node = ImGui::GetWindowDockNode();
    m_rootDock = node ? ImGui::DockNodeGetRootNode(node)->ID : 0;

    ImGui::TextUnformatted(m_filePath.filename().string().c_str());

    const char *icon = m_locked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN;

    float iconW = ImGui::CalcTextSize(icon).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - iconW);
    if (ImGui::Button(icon))
    {
        m_locked = !m_locked;
    }
    ImGui::Separator();

    if (m_filePath.empty())
    {
        ImGui::Text("nothing to Show.");
        return;
    }
    if (fs::exists(m_filePath) == false)
    {
        ImGui::TextDisabled("le chemin du fichier est incorrect.");
        ImGui::TextDisabled("actuel path : %s", m_filePath.string().c_str());
        return;
    }

    if (m_info)
    {
        ImGui::TextDisabled("Priorite: %d   |   Categorie: %s",
                            m_info->priority,
                            m_info->category.empty() ? "-" : m_info->category.c_str());
        ImGui::TextUnformatted("Composants :");
        if (m_info->components.empty())
            ImGui::TextDisabled("   (aucun)");
        else
            for (const std::string &comp : m_info->components)
                ImGui::BulletText("%s", comp.c_str());
        ImGui::Separator();
    }

    m_editor.Render("##code");
}

void ShowCodePanel::changePath(const fs::path &_path)
{
    m_filePath = _path;
    m_title = _path.filename().string() + m_idSuffix;

    fs::path sidecar = _path;
    sidecar.replace_extension(".json");
    m_info = loadSystemInfo(sidecar);

    std::string ext = _path.extension().string();
    if (ext == ".json")
        m_editor.SetLanguageDefinition(TextEditor::LanguageDefinitionId::Json);
    else if (ext == ".cpp" || ext == ".hpp" || ext == ".cc" || ext == ".cxx" || ext == ".hxx")
        m_editor.SetLanguageDefinition(TextEditor::LanguageDefinitionId::Cpp);
    else if (ext == ".h" || ext == ".c")
        m_editor.SetLanguageDefinition(TextEditor::LanguageDefinitionId::C);
    else
        m_editor.SetLanguageDefinition(TextEditor::LanguageDefinitionId::None);

    m_editor.SetText(readFile(_path));
}
