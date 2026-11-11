#include "visu/ui/FsmPanel.hpp"

#include "visu/App.hpp"
#include "visu/core/Project.hpp"
#include "visu/fsm/ActionRegistry.hpp"
#include "visu/fsm/ConditionRegistry.hpp"
#include "visu/fsm/FsmStore.hpp"
#include "visu/helpers/AssetList.hpp"
#include "visu/helpers/NameValidation.hpp"
#include "visu/reflect/JsonWriteVisitor.hpp"
#include "visu/ui/ConfirmModal.hpp"
#include "visu/ui/FsmGraph.hpp"

#include "imgui.h"
#include <imgui_node_editor.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <regex>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
    using namespace fsmgraph;
    using Json = nlohmann::ordered_json;

    const char *kAny = "Any";
    const char *kStateTime = "$stateTime";
    const char *kParamTypes[] = {"bool", "trigger", "int", "float"};
    const char *kOps[] = {"==", "!=", "<", ">", "<=", ">="};

    const ee::fsm::ActionRegistry &editorActions()
    {
        static ee::fsm::ActionRegistry r = []
        {
            ee::fsm::ActionRegistry reg;
            ee::fsm::registerEngineActions(reg);
            return reg;
        }();
        return r;
    }

    const ee::fsm::ConditionRegistry &editorConditions()
    {
        static ee::fsm::ConditionRegistry r;
        return r;
    }

    Json skeleton(const std::string &_name)
    {
        return Json::parse(R"({"version":2,"name":")" + _name +
                           R"(","parameters":[],"states":[{"name":"Idle","actions":[]}],"entryState":"Idle","transitions":[]})");
    }

    Json defaultValue(const std::string &_type)
    {
        if (_type == "int")
            return 0;
        if (_type == "float")
            return 0.0;
        return false;
    }

    std::string str(const Json &_j, const char *_key)
    {
        auto it = _j.find(_key);
        return it != _j.end() && it->is_string() ? it->get<std::string>() : std::string();
    }

    bool isVec3(const Json &_j)
    {
        return _j.is_object() && _j.size() == 3 && _j.contains("x") && _j.contains("y") &&
               _j.contains("z") && _j["x"].is_number() && _j["y"].is_number() && _j["z"].is_number();
    }

    bool isVariant(const Json &_j)
    {
        if (!_j.is_object() || _j.size() != 2 || !_j.contains("value"))
            return false;
        const std::string t = str(_j, "type");
        return t == "bool" || t == "int" || t == "float";
    }

    bool comboString(const char *_label, std::string &_value, const std::vector<std::string> &_items)
    {
        bool changed = false;
        if (ImGui::BeginCombo(_label, _value.c_str()))
        {
            for (const std::string &item : _items)
                if (ImGui::Selectable(item.c_str(), item == _value) && item != _value)
                {
                    _value = item;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool comboJsonString(const char *_label, Json &_j, const std::vector<std::string> &_items)
    {
        std::string v = _j.is_string() ? _j.get<std::string>() : std::string();
        if (!comboString(_label, v, _items))
            return false;
        _j = v;
        return true;
    }

    bool dragNumber(const char *_label, Json &_j, bool _integer)
    {
        if (_integer)
        {
            int v = _j.is_number() ? _j.get<int>() : 0;
            if (!ImGui::DragInt(_label, &v, 0.1f))
                return false;
            _j = v;
            return true;
        }
        float v = _j.is_number() ? _j.get<float>() : 0.0f;
        if (!ImGui::DragFloat(_label, &v, 0.01f, 0.0f, 0.0f, "%.3f"))
            return false;
        _j = ee::reflection::shortestFloat(v);
        return true;
    }

    bool smallButton(const char *_label, const char *_tooltip)
    {
        bool pressed = ImGui::SmallButton(_label);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", _tooltip);
        return pressed;
    }
}

namespace
{
    namespace ed = ax::NodeEditor;
    using namespace fsmgraph;

    const ImU32 kPendingColor = IM_COL32(255, 200, 80, 255);
    constexpr int kKnownVersion = 2;
    constexpr size_t kMaxUndo = 100;

    std::string normalizedPath(const std::string &_p)
    {
        return fs::path(_p).lexically_normal().generic_string();
    }

    void errorList(const std::vector<std::string> &_errors)
    {
        for (const std::string &e : _errors)
            ImGui::TextColored(kErrorColor, "%s", e.c_str());
    }

    std::string conditionSummary(const Json &_c)
    {
        if (_c.contains("custom"))
        {
            const bool expected = !_c.contains("expected") || !_c["expected"].is_boolean() || _c["expected"].get<bool>();
            return std::string(expected ? "" : "!") + str(_c, "custom") + "()";
        }
        std::string s = str(_c, "param");
        if (_c.contains("op"))
            s += " " + str(_c, "op") + " " + (_c.contains("value") ? _c["value"].dump() : std::string("?"));
        return s;
    }
}


FsmPanel::~FsmPanel()
{
    if (m_editor)
        ed::DestroyEditor(m_editor);
}

void FsmPanel::draw(Project *_project)
{
    if (!_project || !_project->isValid())
    {
        ImGui::TextDisabled("Aucun projet ouvert");
        return;
    }
    refresh(_project);

    if (m_selected.empty())
        drawList(_project);
    else
    {
        drawTopBar(_project);
        if (!m_selected.empty())
        {
            if (!m_exists)
                ImGui::TextDisabled("Fichier introuvable : %s", m_selected.c_str());
            else if (!m_parsed)
                drawValidation();
            else
            {
                const float inspectorWidth = 380.0f;
                const float graphWidth = std::max(200.0f, ImGui::GetContentRegionAvail().x - inspectorWidth -
                                                              ImGui::GetStyle().ItemSpacing.x);
                ImGui::BeginChild("##fsm_graph", ImVec2(graphWidth, 0.0f), ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                drawGraph();
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("##fsm_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
                drawInspector();
                ImGui::EndChild();
            }
        }
    }

    if (!m_selected.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        {
            if (m_dirty)
                save(_project);
            undo();
            App::getInstance().claimUndoShortcut();
        }
        else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
                 ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
        {
            redo();
            App::getInstance().claimUndoShortcut();
        }
    }

    if (m_dirty && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        save(_project);
}

void FsmPanel::open(Project *_project, const std::string &_path)
{
    refresh(_project, true);
    select(_project, fs::path(_path).lexically_normal().generic_string());
    setVisible(true);
    ImGui::SetWindowFocus(name());
}

void FsmPanel::close(Project *_project)
{
    if (m_dirty)
        save(_project);
    m_selected.clear();
    m_linking = false;
    m_sel = Sel::None;
    m_selIndex = -1;
    m_status.clear();
}

void FsmPanel::drawTopBar(Project *_project)
{
    if (ImGui::Button("< Retour"))
    {
        close(_project);
        return;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", fs::path(m_selected).stem().string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", m_selected.c_str());
    ImGui::SameLine();
    if (m_errors.empty())
        ImGui::TextColored(kEntryColor, "Valide");
    else
    {
        ImGui::TextColored(kErrorColor, "Invalide");
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            for (const std::string &e : m_errors)
                ImGui::BulletText("%s", e.c_str());
            ImGui::EndTooltip();
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_undo.empty() || m_readOnly);
    if (ImGui::Button("Annuler"))
        undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_redo.empty() || m_readOnly);
    if (ImGui::Button("Retablir"))
        redo();
    ImGui::EndDisabled();

    updateLiveState();
    if (m_liveEntity >= 0)
    {
        ImGui::SameLine();
        std::string who = "entite " + std::to_string(m_liveEntity);
        if (const SceneInfo *snap = App::getInstance().game().snapshot())
            if (m_liveEntity < static_cast<int>(snap->entities.size()) && !snap->entities[m_liveEntity].name.empty())
                who = snap->entities[m_liveEntity].name;
        ImGui::TextColored(kLiveColor, "Play : %s -> %s", who.c_str(),
                           m_liveState.empty() ? "(pas demarree)" : m_liveState.c_str());
    }
    if (m_readOnly)
    {
        ImGui::SameLine();
        ImGui::TextColored(kErrorColor, "Version plus recente : lecture seule");
    }
    if (!m_status.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_status.c_str());
    }
}

ImVec2 FsmPanel::nodePosition(int _state) const
{
    return fsmgraph::nodePosition(m_doc, _state);
}

void FsmPanel::drawGraph()
{
    if (!m_editor)
    {
        ed::Config cfg;
        cfg.SettingsName = nullptr;
        m_editor = ed::CreateEditor(&cfg);
    }
    m_graphHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    ed::SetCurrentEditor(m_editor);
    ed::Begin("##fsm_canvas");
    drawGraphNodes();
    handleGraphInput();
    drawGraphPopups();
    if (m_navigateFrames > 0 && --m_navigateFrames == 0)
        ed::NavigateToContent(0.0f);
    ed::End();
    ed::SetCurrentEditor(nullptr);

    ImGui::SetCursorPos(ImVec2(8.0f, 6.0f));
    ImGui::TextDisabled(m_linking ? "Clic sur un etat pour relier  -  Echap / clic droit : annuler"
                                  : "Clic droit dans le vide : nouvel etat  -  Clic droit sur un etat : transition / supprimer");
}

void FsmPanel::drawGraphNodes()
{
    DrawOptions opt;
    opt.applyPositions = m_applyPositions;
    opt.liveState = m_liveState;
    opt.stateErrors = &m_stateErrors;
    opt.transitionErrors = &m_transitionErrors;
    drawNodesAndLinks(m_doc, opt, m_outPins);
    m_applyPositions = false;

    std::vector<unsigned long long> current;
    if (const int count = ed::GetSelectedObjectCount(); count > 0)
    {
        std::vector<ed::NodeId> nodes(static_cast<size_t>(count));
        std::vector<ed::LinkId> links(static_cast<size_t>(count));
        const int nodeCount = ed::GetSelectedNodes(nodes.data(), count);
        const int linkCount = ed::GetSelectedLinks(links.data(), count);
        for (int i = 0; i < nodeCount; ++i)
            current.push_back(nodes[i].Get());
        for (int i = 0; i < linkCount; ++i)
            current.push_back(links[i].Get());
        std::sort(current.begin(), current.end());
    }
    if (current != m_lastSelection)
    {
        m_lastSelection = current;
        onEditorSelection(current);
    }

    if (m_syncSelection)
    {
        ed::ClearSelection();
        std::vector<unsigned long long> expected;
        if (!m_linking)
        {
            if (m_sel == Sel::State)
                expected.push_back(nodeId(m_selIndex));
            else if (m_sel == Sel::Any)
                expected.push_back(kAnyNode);
            else if (m_sel == Sel::Transition)
                expected.push_back(linkId(m_selIndex));
        }
        for (unsigned long long id : expected)
        {
            if (transitionOfLink(id) >= 0)
                ed::SelectLink(id);
            else
                ed::SelectNode(id);
        }
        m_lastSelection = expected;
        m_syncSelection = false;
    }
}

void FsmPanel::onEditorSelection(const std::vector<unsigned long long> &_ids)
{
    const Json &states = m_doc["states"];
    if (m_linking)
    {
        m_linking = false;
        const int target = _ids.size() == 1 ? stateOfNode(_ids[0]) : -1;
        if (target >= 0 && target != m_linkFrom && target < static_cast<int>(states.size()))
        {
            addTransition(m_linkFrom == kAnyIndex ? kAny : str(states[m_linkFrom], "name"),
                          str(states[target], "name"));
            return;
        }
    }

    if (_ids.size() != 1)
    {
        m_sel = Sel::None;
        m_selIndex = -1;
        return;
    }
    const int s = stateOfNode(_ids[0]);
    const int t = transitionOfLink(_ids[0]);
    if (s == kAnyIndex)
    {
        m_sel = Sel::Any;
        m_selIndex = -1;
    }
    else if (s >= 0)
    {
        m_sel = Sel::State;
        m_selIndex = s;
    }
    else if (t >= 0)
    {
        m_sel = Sel::Transition;
        m_selIndex = t;
    }
}

void FsmPanel::startLinking(int _state)
{
    m_linking = true;
    m_linkFrom = _state;
    m_sel = _state == kAnyIndex ? Sel::Any : Sel::State;
    m_selIndex = _state;
    m_syncSelection = true;
}

void FsmPanel::handleGraphInput()
{
    const Json &states = m_doc["states"];
    const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

    if (ed::BeginCreate(ImGui::ColorConvertU32ToFloat4(kPendingColor)))
    {
        ed::PinId a, b;
        if (ed::QueryNewLink(&a, &b))
        {
            bool aOut = false, bOut = false;
            int sa = stateOfPin(a.Get(), aOut);
            int sb = stateOfPin(b.Get(), bOut);
            if (!aOut && bOut)
            {
                std::swap(sa, sb);
                std::swap(aOut, bOut);
            }
            const bool valid = aOut && !bOut && sa != -1 && sb >= 0;
            if (!valid)
                ed::RejectNewItem(kErrorColor);
            else if (ed::AcceptNewItem())
                addTransition(sa == kAnyIndex ? kAny : str(states[sa], "name"), str(states[sb], "name"));
        }
        ed::EndCreate();
    }

    if (ed::BeginDelete())
    {
        std::vector<int> deletedStates;
        bool anyDeleted = false;
        ed::NodeId n;
        while (ed::QueryDeletedNode(&n))
        {
            ed::AcceptDeletedItem();
            const int s = stateOfNode(n.Get());
            if (s == kAnyIndex)
                anyDeleted = true;
            else if (s >= 0 && s < static_cast<int>(states.size()))
                deletedStates.push_back(s);
        }

        std::vector<std::string> deletedNames;
        for (int s : deletedStates)
            deletedNames.push_back(str(states[s], "name"));
        auto isDeleted = [&](const std::string &_name)
        { return std::find(deletedNames.begin(), deletedNames.end(), _name) != deletedNames.end(); };

        const Json &tr = m_doc["transitions"];
        std::vector<int> deletedTransitions;
        ed::LinkId l;
        while (ed::QueryDeletedLink(&l))
        {
            ed::AcceptDeletedItem();
            const int t = transitionOfLink(l.Get());
            if (t < 0 || t >= static_cast<int>(tr.size()))
                continue;
            if (anyDeleted && str(tr[t], "from") == kAny)
                continue;
            deletedTransitions.push_back(t);
        }
        for (size_t t = 0; t < tr.size(); ++t)
            if (isDeleted(str(tr[t], "from")) || isDeleted(str(tr[t], "to")))
                deletedTransitions.push_back(static_cast<int>(t));

        std::sort(deletedTransitions.rbegin(), deletedTransitions.rend());
        deletedTransitions.erase(std::unique(deletedTransitions.begin(), deletedTransitions.end()),
                                 deletedTransitions.end());
        for (int t : deletedTransitions)
            m_doc["transitions"].erase(m_doc["transitions"].begin() + t);
        std::sort(deletedStates.rbegin(), deletedStates.rend());
        for (int s : deletedStates)
            removeState(s);

        if (!deletedTransitions.empty() || !deletedStates.empty())
        {
            setSelection(Sel::None, -1);
            structureChanged();
        }
        else if (anyDeleted)
        {
            m_applyPositions = true;
            m_syncSelection = true;
        }
        ed::EndDelete();
    }

    if (m_linking && ImGui::IsKeyPressed(ImGuiKey_Escape))
        m_linking = false;

    const unsigned long long hoveredLink = ed::GetHoveredLink().Get();
    if (m_graphHovered && m_linking && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        m_linking = false;
        m_ignoreContext = true;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        bool moved = false;
        for (size_t i = 0; i < states.size(); ++i)
        {
            const int s = static_cast<int>(i);
            const ImVec2 p = ed::GetNodePosition(nodeId(s));
            const ImVec2 stored = nodePosition(s);
            if (std::fabs(p.x - stored.x) > 0.5f || std::fabs(p.y - stored.y) > 0.5f)
            {
                m_doc["states"][i]["editor"] = Json{{"x", std::round(p.x)}, {"y", std::round(p.y)}};
                moved = true;
            }
        }
        const ImVec2 any = ed::GetNodePosition(kAnyNode);
        const ImVec2 anyStored = nodePosition(kAnyIndex);
        if (std::fabs(any.x - anyStored.x) > 0.5f || std::fabs(any.y - anyStored.y) > 0.5f)
        {
            if (!m_doc.contains("editor") || !m_doc["editor"].is_object())
                m_doc["editor"] = Json::object();
            m_doc["editor"]["any"] = Json{{"x", std::round(any.x)}, {"y", std::round(any.y)}};
            moved = true;
        }
        if (moved)
            m_dirty = true;
    }

    if (m_linking)
    {
        auto from = m_outPins.find(m_linkFrom);
        if (from != m_outPins.end())
        {
            const ImVec2 a = from->second;
            const ImVec2 b = ImGui::GetMousePos();
            const float dx = std::max(40.0f, std::fabs(b.x - a.x) * 0.5f);
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddBezierCubic(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, kPendingColor, 2.5f);
            dl->AddCircleFilled(b, 4.0f, kPendingColor);
        }
    }
    else if (hoveredLink && !popupOpen)
    {
        const int t = transitionOfLink(hoveredLink);
        if (t >= 0 && t < static_cast<int>(m_doc["transitions"].size()))
        {
            ed::Suspend();
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(transitionSummary(m_doc["transitions"][t]).c_str());
            if (auto it = m_transitionErrors.find(t); it != m_transitionErrors.end())
                errorList(it->second);
            ImGui::EndTooltip();
            ed::Resume();
        }
    }
    else if (const int hs = stateOfNode(ed::GetHoveredNode().Get()); hs >= 0 && !popupOpen)
    {
        if (auto it = m_stateErrors.find(hs); it != m_stateErrors.end())
        {
            ed::Suspend();
            ImGui::BeginTooltip();
            errorList(it->second);
            ImGui::EndTooltip();
            ed::Resume();
        }
    }
}

void FsmPanel::drawGraphPopups()
{
    const ImVec2 mouse = ImGui::GetMousePos();
    ed::Suspend();
    ed::NodeId contextNode;
    ed::LinkId contextLink;
    if (ed::ShowNodeContextMenu(&contextNode))
    {
        if (!m_ignoreContext)
        {
            m_ctxNode = contextNode.Get();
            ImGui::OpenPopup("fsm_node_ctx");
        }
    }
    else if (ed::ShowLinkContextMenu(&contextLink))
    {
        if (!m_ignoreContext)
        {
            m_ctxLink = contextLink.Get();
            ImGui::OpenPopup("fsm_link_ctx");
        }
    }
    else if (ed::ShowBackgroundContextMenu())
    {
        if (!m_ignoreContext)
            setSelection(Sel::State, addState(mouse));
    }
    if (m_ignoreContext && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        m_ignoreContext = false;

    if (ImGui::BeginPopup("fsm_node_ctx"))
    {
        const int s = stateOfNode(m_ctxNode);
        if (s == kAnyIndex)
        {
            ImGui::TextDisabled("Any");
            ImGui::Separator();
            if (ImGui::MenuItem("Creer une transition"))
                startLinking(kAnyIndex);
        }
        else if (s >= 0 && s < static_cast<int>(m_doc["states"].size()))
        {
            const std::string name = str(m_doc["states"][s], "name");
            ImGui::TextDisabled("%s", name.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Creer une transition"))
                startLinking(s);
            if (ImGui::MenuItem("Etat d'entree", nullptr, false, str(m_doc, "entryState") != name))
            {
                m_doc["entryState"] = name;
                changed();
            }
            if (ImGui::MenuItem("Supprimer"))
            {
                removeState(s);
                setSelection(Sel::None, -1);
                structureChanged();
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("fsm_link_ctx"))
    {
        const int t = transitionOfLink(m_ctxLink);
        if (t >= 0 && t < static_cast<int>(m_doc["transitions"].size()))
        {
            ImGui::TextDisabled("%s", transitionSummary(m_doc["transitions"][t]).c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Supprimer"))
                removeTransition(t);
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}

void FsmPanel::setSelection(Sel _kind, int _index)
{
    m_sel = _kind;
    m_selIndex = _index;
    m_syncSelection = true;
}

void FsmPanel::structureChanged()
{
    m_applyPositions = true;
    m_syncSelection = true;
    changed();
}

int FsmPanel::addState(const ImVec2 &_pos)
{
    std::vector<std::string> names = stateNames();
    std::string n = "State";
    for (int k = 1; std::find(names.begin(), names.end(), n) != names.end(); ++k)
        n = "State" + std::to_string(k);
    m_doc["states"].push_back(Json{{"name", n},
                                   {"actions", Json::array()},
                                   {"editor", Json{{"x", std::round(_pos.x)}, {"y", std::round(_pos.y)}}}});
    if (str(m_doc, "entryState").empty())
        m_doc["entryState"] = n;
    structureChanged();
    return static_cast<int>(m_doc["states"].size()) - 1;
}

void FsmPanel::addTransition(const std::string &_from, const std::string &_to)
{
    m_doc["transitions"].push_back(Json{{"from", _from}, {"to", _to}, {"conditions", Json::array()}});
    setSelection(Sel::Transition, static_cast<int>(m_doc["transitions"].size()) - 1);
    changed();
}

void FsmPanel::removeTransition(int _index)
{
    Json &tr = m_doc["transitions"];
    if (_index < 0 || _index >= static_cast<int>(tr.size()))
        return;
    tr.erase(tr.begin() + _index);
    setSelection(Sel::None, -1);
    changed();
}

void FsmPanel::moveTransition(int _index, int _dir)
{
    Json &tr = m_doc["transitions"];
    const std::string from = str(tr[_index], "from");
    for (int j = _index + _dir; j >= 0 && j < static_cast<int>(tr.size()); j += _dir)
        if (str(tr[j], "from") == from)
        {
            std::swap(tr[_index], tr[j]);
            setSelection(Sel::Transition, j);
            changed();
            return;
        }
}

std::string FsmPanel::transitionSummary(const Json &_t) const
{
    std::string s = str(_t, "from") + " -> " + str(_t, "to");
    if (!_t.contains("conditions") || _t["conditions"].empty())
        return s + "\nsans condition (immediate)";
    s += "\nsi ";
    bool first = true;
    for (const Json &c : _t["conditions"])
    {
        s += (first ? "" : "\net ") + conditionSummary(c);
        first = false;
    }
    return s;
}

void FsmPanel::drawInspector()
{
    const int states = static_cast<int>(m_doc["states"].size());
    const int transitions = static_cast<int>(m_doc["transitions"].size());
    if ((m_sel == Sel::State && (m_selIndex < 0 || m_selIndex >= states)) ||
        (m_sel == Sel::Transition && (m_selIndex < 0 || m_selIndex >= transitions)))
        m_sel = Sel::None;

    switch (m_sel)
    {
    case Sel::State:
        drawInspectorState(m_selIndex);
        break;
    case Sel::Any:
        drawInspectorAny();
        break;
    case Sel::Transition:
        drawInspectorTransition(m_selIndex);
        break;
    case Sel::None:
        drawInspectorNone();
        break;
    }
}

void FsmPanel::drawInspectorNone()
{
    drawValidation();
    drawGeneral();
    drawParameters();
    ImGui::Spacing();
    ImGui::SeparatorText("Graphe");
    ImGui::TextDisabled("Clic droit dans le vide : nouvel etat\n"
                        "Clic sur un etat / une transition : le selectionner\n"
                        "Clic droit sur un etat : creer une transition, puis\n"
                        "  clic sur l'etat cible pour relier\n"
                        "Echap / clic droit : annuler la transition\n"
                        "Clic droit sur un etat / une transition : supprimer\n"
                        "Glisser un etat : le deplacer\n"
                        "Molette : zoom, clic droit glisse : deplacer la vue");
}

void FsmPanel::drawInspectorState(int _index)
{
    Json &state = m_doc["states"][_index];
    const std::string name = str(state, "name");
    const bool isEntry = str(m_doc, "entryState") == name;

    ImGui::TextDisabled("Etat");
    if (auto it = m_stateErrors.find(_index); it != m_stateErrors.end())
        errorList(it->second);
    ImGui::PushItemWidth(-90.0f);
    std::string renamed;
    if (editName("Nom", name, renamed))
    {
        std::vector<std::string> names = stateNames();
        if (renamed == kAny)
            m_status = "\"Any\" est reserve.";
        else if (std::find(names.begin(), names.end(), renamed) != names.end())
            m_status = "\"" + renamed + "\" existe deja.";
        else
        {
            state["name"] = renamed;
            renameState(name, renamed);
            changed();
        }
    }
    ImGui::PopItemWidth();
    if (isEntry)
        ImGui::TextColored(kEntryColor, "Etat d'entree");
    else if (ImGui::Button("Definir comme etat d'entree"))
    {
        m_doc["entryState"] = name;
        changed();
    }

    ImGui::SeparatorText("Actions");
    Json &actions = state["actions"];
    int removeAt = -1;
    for (size_t a = 0; a < actions.size(); ++a)
    {
        ImGui::PushID(static_cast<int>(a));
        if (smallButton("X", "Supprimer l'action"))
            removeAt = static_cast<int>(a);
        ImGui::SameLine();
        if (drawAction(actions[a]))
            changed();
        ImGui::PopID();
    }
    if (removeAt >= 0)
    {
        actions.erase(actions.begin() + removeAt);
        changed();
    }
    if (ImGui::Button("+ Action"))
        ImGui::OpenPopup("add_action");
    if (ImGui::BeginPopup("add_action"))
    {
        for (const auto &[type, tpl] : m_actionTemplates)
            if (ImGui::Selectable(type.c_str()))
            {
                actions.push_back(tpl);
                changed();
            }
        ImGui::Separator();
        ImGui::SetNextItemWidth(160.0f);
        bool enter = ImGui::InputTextWithHint("##type", "Autre type...", &m_newActionType,
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Ajouter") || enter) && !m_newActionType.empty())
        {
            actions.push_back(Json{{"type", m_newActionType}});
            m_newActionType.clear();
            changed();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SeparatorText("Transitions sortantes (priorite)");
    drawTransitionList(name);

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Supprimer l'etat"))
    {
        removeState(_index);
        setSelection(Sel::None, -1);
        structureChanged();
    }
}

void FsmPanel::drawInspectorAny()
{
    ImGui::TextColored(kAnyColor, "Any");
    ImGui::TextDisabled("Transitions testees depuis n'importe quel etat,\navant celles de l'etat courant.");
    ImGui::SeparatorText("Transitions (priorite)");
    drawTransitionList(kAny);
}

void FsmPanel::drawTransitionList(const std::string &_from)
{
    const Json &tr = m_doc["transitions"];
    int rank = 0;
    for (size_t t = 0; t < tr.size(); ++t)
    {
        if (str(tr[t], "from") != _from)
            continue;
        ++rank;
        const size_t conds = tr[t].contains("conditions") ? tr[t]["conditions"].size() : 0;
        std::string label = std::to_string(rank) + ". -> " + str(tr[t], "to") + "   (" + std::to_string(conds) +
                            " condition" + (conds > 1 ? "s" : "") + ")##t" + std::to_string(t);
        if (ImGui::Selectable(label.c_str(), false))
            setSelection(Sel::Transition, static_cast<int>(t));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", transitionSummary(tr[t]).c_str());
    }
    if (rank == 0)
        ImGui::TextDisabled("Aucune. Clic droit sur le noeud : Creer une transition.");
}

void FsmPanel::drawInspectorTransition(int _index)
{
    Json &t = m_doc["transitions"][_index];
    const std::string from = str(t, "from");

    ImGui::TextDisabled("Transition");
    if (auto it = m_transitionErrors.find(_index); it != m_transitionErrors.end())
        errorList(it->second);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("De");
    ImGui::SameLine(60.0f);
    const std::vector<std::string> names = stateNames();
    auto fromIt = std::find(names.begin(), names.end(), from);
    if (ImGui::Button((from + "##from").c_str()))
    {
        if (from == kAny)
            setSelection(Sel::Any, -1);
        else if (fromIt != names.end())
            setSelection(Sel::State, static_cast<int>(fromIt - names.begin()));
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Vers");
    ImGui::SameLine(60.0f);
    ImGui::SetNextItemWidth(-1.0f);
    if (comboJsonString("##to", t["to"], names))
        changed();

    int rank = 0, count = 0;
    const Json &tr = m_doc["transitions"];
    for (size_t i = 0; i < tr.size(); ++i)
        if (str(tr[i], "from") == from)
        {
            ++count;
            if (static_cast<int>(i) == _index)
                rank = count;
        }
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Priorite %d / %d", rank, count);
    ImGui::SameLine();
    ImGui::BeginDisabled(rank <= 1);
    if (ImGui::ArrowButton("##up", ImGuiDir_Up))
        moveTransition(_index, -1);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(rank >= count);
    if (ImGui::ArrowButton("##down", ImGuiDir_Down))
        moveTransition(_index, +1);
    ImGui::EndDisabled();
    if (m_sel != Sel::Transition || m_selIndex != _index)
        return;

    if (from == kAny)
    {
        bool self = t.contains("allowSelf") && t["allowSelf"].is_boolean() && t["allowSelf"].get<bool>();
        if (ImGui::Checkbox("allowSelf", &self))
        {
            if (self)
                t["allowSelf"] = true;
            else
                t.erase("allowSelf");
            changed();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Autorise la transition quand l'etat courant est deja la cible");
    }

    ImGui::SeparatorText("Conditions (toutes vraies)");
    Json &conds = t["conditions"];
    if (conds.empty())
        ImGui::TextDisabled("Sans condition : la transition part tout de suite.");
    int removeCond = -1;
    int swapCond = -1;
    for (size_t c = 0; c < conds.size(); ++c)
    {
        ImGui::PushID(static_cast<int>(c));
        if (smallButton("x", "Supprimer la condition"))
            removeCond = static_cast<int>(c);
        ImGui::SameLine();
        ImGui::BeginDisabled(c == 0);
        if (ImGui::ArrowButton("##cup", ImGuiDir_Up))
            swapCond = static_cast<int>(c) - 1;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(c + 1 == conds.size());
        if (ImGui::ArrowButton("##cdown", ImGuiDir_Down))
            swapCond = static_cast<int>(c);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (drawCondition(conds[c]))
            changed();
        ImGui::PopID();
    }
    if (swapCond >= 0)
    {
        std::swap(conds[swapCond], conds[swapCond + 1]);
        changed();
    }
    if (removeCond >= 0)
    {
        conds.erase(conds.begin() + removeCond);
        changed();
    }
    if (ImGui::Button("+ Condition"))
    {
        std::vector<std::string> params = paramNames();
        conds.push_back(params.empty() ? Json{{"param", kStateTime}, {"op", ">="}, {"value", 0.5}}
                                       : Json{{"param", params[0]}});
        changed();
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Supprimer la transition"))
        removeTransition(_index);
}

void FsmPanel::drawList(Project *_project)
{
    ImGui::SetNextItemWidth(-70.0f);
    bool create = ImGui::InputTextWithHint("##newfsm", "Nouvelle machine...", &m_newName,
                                           ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool valid = namevalidation::isValidIdentifier(m_newName);
    ImGui::BeginDisabled(!valid);
    create = ImGui::Button("Creer") || create;
    ImGui::EndDisabled();
    if (create && valid)
    {
        std::string rel = "Assets/StateMachines/" + m_newName + ".fsm";
        fs::path file = _project->root() / rel;
        std::error_code ec;
        fs::create_directories(file.parent_path(), ec);
        bool written = false;
        if (!fs::exists(file))
        {
            std::ofstream f(file, std::ios::binary);
            written = static_cast<bool>(f << skeleton(m_newName).dump(4) << "\n");
        }
        if (fs::exists(file) && !written)
            m_status = "\"" + m_newName + "\" existe deja.";
        else if (written)
        {
            m_status = "Machine creee.";
            m_newName.clear();
            refresh(_project, true);
            select(_project, rel);
        }
        else
            m_status = "Ecriture impossible.";
    }
    if (!m_newName.empty() && !valid)
        ImGui::TextColored(kErrorColor, "Nom : lettres, chiffres, _");
    if (!m_status.empty())
        ImGui::TextDisabled("%s", m_status.c_str());
    ImGui::Separator();

    if (m_files.empty())
        ImGui::TextDisabled("Aucune machine (Assets/**/*.fsm)");
    for (const std::string &f : m_files)
    {
        std::string label = fs::path(f).stem().string() + "##" + f;
        if (ImGui::Selectable(label.c_str(), f == m_selected))
            select(_project, f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nGlisser sur le champ FSM Path d'un StateMachineComponent", f.c_str());

        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(kAssetPayload, f.c_str(), f.size() + 1);
            ImGui::TextUnformatted(fs::path(f).stem().string().c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Supprimer"))
            {
                fs::path file = _project->root() / f;
                std::string rel = f;
                Project *proj = _project;
                App::getInstance().openModal(std::make_unique<ConfirmModal>(
                    "Supprimer la machine ?",
                    "Supprimer " + rel + " ?\nLes entites qui l'utilisent n'auront plus de machine.",
                    std::vector<ModalChoice>{{"Supprimer",
                                              [this, file, rel, proj]()
                                              {
                                                  std::error_code ec;
                                                  fs::remove(file, ec);
                                                  if (m_selected == rel)
                                                  {
                                                      m_selected.clear();
                                                      m_dirty = false;
                                                  }
                                                  m_status = ec ? "Suppression impossible." : "Machine supprimee.";
                                                  refresh(proj, true);
                                              }},
                                             {"Annuler", nullptr}}));
            }
            ImGui::EndPopup();
        }
    }
}

void FsmPanel::refresh(Project *_project, bool _force)
{
    auto now = std::chrono::steady_clock::now();
    if (!_force && now - m_lastRefresh < std::chrono::seconds(1))
        return;
    m_lastRefresh = now;

    std::vector<std::string> files = listAssets(_project->root(), {".fsm"});
    if (files != m_files || _force)
    {
        m_files = std::move(files);
        rebuildTemplates(_project);
    }

    if (m_selected.empty())
        return;
    std::error_code ec;
    m_exists = fs::exists(_project->root() / m_selected, ec) && !ec;

    if (m_exists && !m_dirty && selectedMtime(_project) != m_mtime)
    {
        std::string keep = m_selected;
        select(_project, keep);
        m_status = "Recharge (modifie hors de l'editeur).";
    }
}

void FsmPanel::rebuildTemplates(Project *_project)
{
    m_actionTemplates.clear();
    for (const char *type : {"SetParam", "Idle"})
        if (std::unique_ptr<ee::fsm::Action> a = editorActions().create(type))
        {
            Json j = Json::object();
            j["type"] = type;
            ee::reflection::JsonWriteVisitor writer(&j);
            a->VisitParams(writer);
            m_actionTemplates[type] = j;
        }

    for (const std::string &f : m_files)
    {
        std::ifstream in(_project->root() / f);
        Json doc = Json::parse(in, nullptr, false);
        if (!doc.is_object() || !doc.contains("states") || !doc["states"].is_array())
            continue;
        for (const Json &s : doc["states"])
        {
            if (!s.is_object() || !s.contains("actions") || !s["actions"].is_array())
                continue;
            for (const Json &a : s["actions"])
            {
                const std::string type = str(a, "type");
                if (!type.empty() && !m_actionTemplates.count(type))
                    m_actionTemplates[type] = a;
            }
        }
    }
}

fs::file_time_type FsmPanel::selectedMtime(Project *_project) const
{
    std::error_code ec;
    auto t = fs::last_write_time(_project->root() / m_selected, ec);
    return ec ? fs::file_time_type{} : t;
}

void FsmPanel::select(Project *_project, const std::string &_path)
{
    if (m_dirty && _path != m_selected)
        save(_project);
    const bool newFile = _path != m_selected;
    m_selected = _path;
    m_dirty = false;
    m_linking = false;
    m_applyPositions = true;
    m_sel = Sel::None;
    m_selIndex = -1;
    m_syncSelection = true;
    if (newFile)
        m_navigateFrames = 3;
    m_mtime = selectedMtime(_project);

    std::ifstream in(_project->root() / _path);
    m_exists = static_cast<bool>(in);
    std::stringstream ss;
    ss << in.rdbuf();
    m_doc = Json::parse(ss.str(), nullptr, false);
    m_parsed = m_doc.is_object();
    if (m_parsed)
        normalize();
    m_committed = m_doc;
    if (newFile)
    {
        m_undo.clear();
        m_redo.clear();
    }
    validate();
}

void FsmPanel::save(Project *_project)
{
    m_dirty = false;
    if (m_selected.empty() || !m_parsed)
        return;
    if (m_readOnly)
    {
        m_status = "Version plus recente que l'editeur : fichier non ecrit.";
        return;
    }
    const fs::path file = _project->root() / m_selected;

    if (selectedMtime(_project) != m_mtime)
    {
        std::string keep = m_selected;
        select(_project, keep);
        m_status = "Modifie hors de l'editeur : retouche annulee, fichier recharge.";
        return;
    }
    std::ofstream f(file, std::ios::binary | std::ios::trunc);
    if (!(f << m_doc.dump(4) << "\n"))
        m_status = "Ecriture impossible : " + m_selected;
    f.close();
    m_mtime = selectedMtime(_project);

    if (m_doc != m_committed)
    {
        m_undo.push_back(m_committed);
        if (m_undo.size() > kMaxUndo)
            m_undo.erase(m_undo.begin());
        m_redo.clear();
        m_committed = m_doc;
    }
}

void FsmPanel::normalize()
{
    if (!m_doc.contains("version"))
        m_doc["version"] = 2;
    for (const char *key : {"parameters", "states", "transitions"})
        if (!m_doc.contains(key) || !m_doc[key].is_array())
            m_doc[key] = Json::array();
    for (Json &s : m_doc["states"])
        if (s.is_object() && (!s.contains("actions") || !s["actions"].is_array()))
            s["actions"] = Json::array();
    for (Json &t : m_doc["transitions"])
        if (t.is_object() && (!t.contains("conditions") || !t["conditions"].is_array()))
            t["conditions"] = Json::array();
}

void FsmPanel::validate()
{
    m_errors.clear();
    m_warnings.clear();
    m_unknownTypes.clear();
    if (!m_parsed)
    {
        m_errors.push_back("JSON invalide : corrige le fichier a la main.");
        return;
    }
    std::vector<std::string> warnings;
    m_readOnly = m_doc.contains("version") && m_doc["version"].is_number_integer() &&
                 m_doc["version"].get<int>() > kKnownVersion;
    ee::fsm::fsmFromJson(nlohmann::json::parse(m_doc.dump()), {editorActions(), editorConditions()},
                         m_errors, &warnings);

    for (const std::string &w : warnings)
    {
        const size_t q = w.find('"');
        const bool unknown = w.find("isn't registered") != std::string::npos && q != std::string::npos;
        if (!unknown)
        {
            m_warnings.push_back(w);
            continue;
        }
        const std::string name = w.substr(q + 1, w.find('"', q + 1) - q - 1);
        if (std::find(m_unknownTypes.begin(), m_unknownTypes.end(), name) == m_unknownTypes.end())
            m_unknownTypes.push_back(name);
    }
    mapErrors();
}

void FsmPanel::changed()
{
    m_dirty = true;
    validate();
}

std::vector<std::string> FsmPanel::stateNames() const
{
    std::vector<std::string> out;
    for (const Json &s : m_doc["states"])
        out.push_back(str(s, "name"));
    return out;
}

std::vector<std::string> FsmPanel::paramNames() const
{
    std::vector<std::string> out;
    for (const Json &p : m_doc["parameters"])
        out.push_back(str(p, "name"));
    return out;
}

std::string FsmPanel::paramType(const std::string &_name) const
{
    for (const Json &p : m_doc["parameters"])
        if (str(p, "name") == _name)
            return str(p, "type");
    return "";
}

void FsmPanel::renameState(const std::string &_old, const std::string &_new)
{
    if (str(m_doc, "entryState") == _old)
        m_doc["entryState"] = _new;
    for (Json &t : m_doc["transitions"])
    {
        if (str(t, "from") == _old)
            t["from"] = _new;
        if (str(t, "to") == _old)
            t["to"] = _new;
    }
}

void FsmPanel::renameParam(const std::string &_old, const std::string &_new)
{
    for (Json &t : m_doc["transitions"])
        for (Json &c : t["conditions"])
            if (str(c, "param") == _old)
                c["param"] = _new;
    for (Json &s : m_doc["states"])
        for (Json &a : s["actions"])
            if (str(a, "type") == "SetParam" && str(a, "param") == _old)
                a["param"] = _new;
}

void FsmPanel::removeState(int _index)
{
    const std::string name = str(m_doc["states"][_index], "name");
    m_doc["states"].erase(m_doc["states"].begin() + _index);
    Json &tr = m_doc["transitions"];
    for (size_t i = tr.size(); i-- > 0;)
        if (str(tr[i], "from") == name || str(tr[i], "to") == name)
            tr.erase(tr.begin() + static_cast<long>(i));
    if (str(m_doc, "entryState") == name)
        m_doc["entryState"] = m_doc["states"].empty() ? "" : str(m_doc["states"][0], "name");
}

bool FsmPanel::editName(const char *_label, const std::string &_current, std::string &_out)
{
    const ImGuiID id = ImGui::GetID(_label);
    std::string tmp = m_editId == id ? m_editBuf : _current;
    ImGui::InputText(_label, &tmp);
    if (ImGui::IsItemActive())
    {
        m_editId = id;
        m_editBuf = tmp;
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        m_editId = 0;
        if (tmp != _current && !tmp.empty())
        {
            _out = tmp;
            return true;
        }
    }
    else if (!ImGui::IsItemActive() && m_editId == id)
        m_editId = 0;
    return false;
}

void FsmPanel::drawValidation()
{
    if (m_errors.empty())
        ImGui::TextColored(kEntryColor, "Valide");
    else
    {
        ImGui::TextColored(kErrorColor, "Invalide");
        for (const std::string &e : m_errors)
            ImGui::BulletText("%s", e.c_str());
    }
    for (const std::string &w : m_warnings)
        ImGui::TextDisabled("warning : %s", w.c_str());
    if (!m_unknownTypes.empty())
    {
        std::string list;
        for (const std::string &t : m_unknownTypes)
            list += (list.empty() ? "" : ", ") + t;
        ImGui::TextDisabled("Defini dans le jeu : %s", list.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Actions / conditions C++ du jeu, inconnues de l'editeur.\n"
                              "Le jeu doit les enregistrer avant de charger la scene.");
    }
    ImGui::Separator();
}

void FsmPanel::drawGeneral()
{
    if (!ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    ImGui::PushItemWidth(-110.0f);

    std::string name = str(m_doc, "name");
    if (ImGui::InputText("Nom", &name))
    {
        m_doc["name"] = name;
        changed();
    }

    std::string entry = str(m_doc, "entryState");
    if (comboString("Etat d'entree", entry, stateNames()))
    {
        m_doc["entryState"] = entry;
        changed();
    }
    ImGui::PopItemWidth();
}

void FsmPanel::drawParameters()
{
    Json &params = m_doc["parameters"];
    std::string header = "Parametres (" + std::to_string(params.size()) + ")###params";
    if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        return;

    int removeAt = -1;
    if (ImGui::BeginTable("##params", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Nom", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Defaut", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < params.size(); ++i)
        {
            Json &p = params[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            std::string renamed;
            const std::string old = str(p, "name");
            const auto paramErr = m_paramErrors.find(static_cast<int>(i));
            if (paramErr != m_paramErrors.end())
                ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
            const bool renamedOk = editName("##name", old, renamed);
            if (paramErr != m_paramErrors.end())
            {
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    errorList(paramErr->second);
                    ImGui::EndTooltip();
                }
            }
            if (renamedOk)
            {
                std::vector<std::string> names = paramNames();
                if (renamed[0] == '$')
                    m_status = "Le prefixe $ est reserve.";
                else if (std::find(names.begin(), names.end(), renamed) != names.end())
                    m_status = "\"" + renamed + "\" existe deja.";
                else
                {
                    p["name"] = renamed;
                    renameParam(old, renamed);
                    changed();
                }
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            std::string type = str(p, "type");
            if (comboString("##type", type, {std::begin(kParamTypes), std::end(kParamTypes)}))
            {
                p["type"] = type;
                if (type == "trigger")
                    p.erase("default");
                else
                    p["default"] = defaultValue(type);
                changed();
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (type == "trigger")
                ImGui::TextDisabled("false");
            else if (type == "bool")
            {
                bool v = p.contains("default") && p["default"].is_boolean() && p["default"].get<bool>();
                if (ImGui::Checkbox("##def", &v))
                {
                    p["default"] = v;
                    changed();
                }
            }
            else if (type == "int" || type == "float")
            {
                if (!p.contains("default"))
                    p["default"] = defaultValue(type);
                if (dragNumber("##def", p["default"], type == "int"))
                    changed();
            }

            ImGui::TableNextColumn();
            if (smallButton("X", "Supprimer le parametre"))
                removeAt = static_cast<int>(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeAt >= 0)
    {
        params.erase(params.begin() + removeAt);
        changed();
    }

    if (ImGui::Button("+ Parametre"))
    {
        std::vector<std::string> names = paramNames();
        std::string n = "param";
        for (int k = 1; std::find(names.begin(), names.end(), n) != names.end(); ++k)
            n = "param" + std::to_string(k);
        params.push_back(Json{{"name", n}, {"type", "trigger"}});
        changed();
    }
}

bool FsmPanel::drawCondition(Json &_cond)
{
    if (!_cond.is_object())
    {
        ImGui::TextColored(kErrorColor, "condition invalide");
        return false;
    }
    bool edited = false;
    const bool custom = _cond.contains("custom");
    const std::string param = str(_cond, "param");

    std::vector<std::string> sources = paramNames();
    sources.push_back(kStateTime);
    sources.push_back("(custom C++)");
    std::string source = custom ? "(custom C++)" : param;

    ImGui::SetNextItemWidth(130.0f);
    if (comboString("##src", source, sources))
    {
        if (source == "(custom C++)")
            _cond = Json{{"custom", "IsGrounded"}};
        else if (source == kStateTime)
            _cond = Json{{"param", kStateTime}, {"op", ">="}, {"value", 0.5}};
        else
        {
            const std::string type = paramType(source);
            _cond = type == "bool" || type == "trigger"
                        ? Json{{"param", source}}
                        : Json{{"param", source}, {"op", ">"}, {"value", defaultValue(type)}};
        }
        return true;
    }

    if (custom)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        std::string name = str(_cond, "custom");
        if (ImGui::InputText("##custom", &name))
        {
            _cond["custom"] = name;
            edited = true;
        }
        ImGui::SameLine();
        bool expected = !_cond.contains("expected") || !_cond["expected"].is_boolean() || _cond["expected"].get<bool>();
        if (ImGui::Checkbox("attendu", &expected))
        {
            if (expected)
                _cond.erase("expected");
            else
                _cond["expected"] = false;
            edited = true;
        }
        return edited;
    }

    const std::string type = param == kStateTime ? "float" : paramType(param);
    if (type == "bool" || type == "trigger")
    {
        const bool isFalse = _cond.contains("value") && _cond["value"].is_boolean() && !_cond["value"].get<bool>() &&
                             str(_cond, "op") == "==";
        int choice = isFalse ? 1 : 0;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::Combo("##bool", &choice, "vrai\0faux\0"))
        {
            _cond.erase("op");
            _cond.erase("value");
            if (choice == 1)
            {
                _cond["op"] = "==";
                _cond["value"] = false;
            }
            edited = true;
        }
        return edited;
    }
    if (type.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(kErrorColor, "parametre inconnu");
        return false;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(55.0f);
    if (!_cond.contains("op"))
        _cond["op"] = ">";
    if (comboJsonString("##op", _cond["op"], {std::begin(kOps), std::end(kOps)}))
        edited = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    if (!_cond.contains("value"))
        _cond["value"] = defaultValue(type);
    if (dragNumber("##value", _cond["value"], type == "int"))
        edited = true;
    return edited;
}

bool FsmPanel::drawAction(Json &_action)
{
    if (!_action.is_object())
    {
        ImGui::TextColored(kErrorColor, "action invalide");
        return false;
    }
    const std::string type = str(_action, "type");
    const bool known = std::find(m_unknownTypes.begin(), m_unknownTypes.end(), type) == m_unknownTypes.end();
    const std::string label = type + (known ? "" : "  (jeu)") + "###action";
    bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (!open)
        return false;

    bool edited = false;
    std::string removeKey;
    ImGui::PushItemWidth(-140.0f);
    for (auto it = _action.begin(); it != _action.end(); ++it)
    {
        if (it.key() == "type")
            continue;
        ImGui::PushID(it.key().c_str());
        if (smallButton("-", "Retirer ce reglage"))
            removeKey = it.key();
        ImGui::SameLine();
        edited |= drawValue(it.key(), it.value());
        ImGui::PopID();
    }
    ImGui::PopItemWidth();
    if (!removeKey.empty())
    {
        _action.erase(removeKey);
        edited = true;
    }
    edited |= drawAddField(_action);
    ImGui::TreePop();
    return edited;
}

bool FsmPanel::drawAddField(Json &_action)
{
    if (ImGui::SmallButton("+ reglage"))
        ImGui::OpenPopup("add_field");
    bool added = false;
    if (ImGui::BeginPopup("add_field"))
    {
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputTextWithHint("##field", "nom", &m_newFieldName);
        const bool ok = !m_newFieldName.empty() && m_newFieldName != "type" && !_action.contains(m_newFieldName);
        ImGui::BeginDisabled(!ok);
        Json value;
        if (ImGui::Button("bool"))
            value = false;
        ImGui::SameLine();
        if (ImGui::Button("nombre"))
            value = 0.0;
        ImGui::SameLine();
        if (ImGui::Button("texte"))
            value = "";
        ImGui::SameLine();
        if (ImGui::Button("vec3"))
            value = Json{{"x", 0.0}, {"y", 0.0}, {"z", 0.0}};
        ImGui::EndDisabled();
        if (!value.is_null())
        {
            _action[m_newFieldName] = value;
            m_newFieldName.clear();
            added = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return added;
}

bool FsmPanel::drawValue(const std::string &_label, Json &_value)
{
    const char *label = _label.c_str();
    if (_value.is_boolean())
    {
        bool v = _value.get<bool>();
        if (!ImGui::Checkbox(label, &v))
            return false;
        _value = v;
        return true;
    }
    if (_value.is_number())
        return dragNumber(label, _value, _value.is_number_integer());
    if (_value.is_string())
    {
        std::string v = _value.get<std::string>();
        if (!ImGui::InputText(label, &v))
            return false;
        _value = v;
        return true;
    }
    if (isVec3(_value))
    {
        float v[3] = {_value["x"].get<float>(), _value["y"].get<float>(), _value["z"].get<float>()};
        if (!ImGui::DragFloat3(label, v, 0.01f, 0.0f, 0.0f, "%.3f"))
            return false;
        _value["x"] = ee::reflection::shortestFloat(v[0]);
        _value["y"] = ee::reflection::shortestFloat(v[1]);
        _value["z"] = ee::reflection::shortestFloat(v[2]);
        return true;
    }
    if (isVariant(_value))
    {
        bool edited = false;
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(70.0f);
        std::string t = str(_value, "type");
        const std::string id = "##vt" + _label;
        if (comboString(id.c_str(), t, {"bool", "int", "float"}))
        {
            _value["type"] = t;
            _value["value"] = defaultValue(t);
            edited = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-140.0f);
        if (t == "bool")
        {
            bool v = _value["value"].is_boolean() && _value["value"].get<bool>();
            if (ImGui::Checkbox(label, &v))
            {
                _value["value"] = v;
                edited = true;
            }
        }
        else
            edited |= dragNumber(label, _value["value"], t == "int");
        ImGui::EndGroup();
        return edited;
    }
    if (_value.is_object() || _value.is_array())
    {
        bool edited = false;
        if (ImGui::TreeNode(label))
        {
            if (_value.is_object())
                for (auto it = _value.begin(); it != _value.end(); ++it)
                {
                    ImGui::PushID(it.key().c_str());
                    edited |= drawValue(it.key(), it.value());
                    ImGui::PopID();
                }
            else
                for (size_t i = 0; i < _value.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    edited |= drawValue(std::to_string(i), _value[i]);
                    ImGui::PopID();
                }
            ImGui::TreePop();
        }
        return edited;
    }
    ImGui::TextDisabled("%s : null", label);
    return false;
}

void FsmPanel::mapErrors()
{
    m_stateErrors.clear();
    m_transitionErrors.clear();
    m_paramErrors.clear();
    if (!m_parsed)
        return;
    static const std::regex stateIdx(R"(^states\[(\d+)\])");
    static const std::regex stateName(R"(^state (\S+): )");
    static const std::regex transIdx(R"(^transitions\[(\d+)\])");
    static const std::regex paramIdx(R"(^parameters\[(\d+)\])");
    const std::vector<std::string> names = stateNames();
    for (const std::string &e : m_errors)
    {
        std::smatch m;
        if (std::regex_search(e, m, stateIdx))
            m_stateErrors[std::stoi(m[1])].push_back(e);
        else if (std::regex_search(e, m, stateName))
        {
            auto it = std::find(names.begin(), names.end(), m[1].str());
            if (it != names.end())
                m_stateErrors[static_cast<int>(it - names.begin())].push_back(e);
        }
        else if (std::regex_search(e, m, transIdx))
            m_transitionErrors[std::stoi(m[1])].push_back(e);
        else if (std::regex_search(e, m, paramIdx))
            m_paramErrors[std::stoi(m[1])].push_back(e);
    }
}

void FsmPanel::undo()
{
    if (m_undo.empty() || m_readOnly)
        return;
    m_redo.push_back(m_doc);
    m_doc = m_undo.back();
    m_undo.pop_back();
    m_committed = m_doc;
    m_linking = false;
    setSelection(Sel::None, -1);
    structureChanged();
}

void FsmPanel::redo()
{
    if (m_redo.empty() || m_readOnly)
        return;
    m_undo.push_back(m_doc);
    m_doc = m_redo.back();
    m_redo.pop_back();
    m_committed = m_doc;
    m_linking = false;
    setSelection(Sel::None, -1);
    structureChanged();
}

void FsmPanel::updateLiveState()
{
    m_liveState.clear();
    m_liveEntity = -1;
    GameProcess &game = App::getInstance().game();
    if (!game.running())
        return;
    const std::string mine = normalizedPath(m_selected);
    const int selected = App::getInstance().selectedEntity();
    for (const ee::ipc::FsmLiveState &f : game.fsmStates())
    {
        if (normalizedPath(f.path) != mine)
            continue;
        if (m_liveEntity < 0 || f.entity == selected)
        {
            m_liveEntity = f.entity;
            m_liveState = f.state;
        }
        if (f.entity == selected)
            break;
    }
}
