#include "visu/ui/FsmInspector.hpp"

#include "visu/App.hpp"
#include "visu/core/Project.hpp"
#include "visu/reflect/JsonWriteVisitor.hpp"
#include "visu/ui/FsmGraph.hpp"
#include "visu/ui/FsmPanel.hpp"

#include "imgui.h"
#include <imgui_node_editor.h>

#include <filesystem>
#include <fstream>
#include <map>

namespace fs = std::filesystem;
namespace ed = ax::NodeEditor;
using Json = nlohmann::ordered_json;

namespace
{
    struct Preview
    {
        std::string path;
        fs::file_time_type mtime{};
        Json doc;
        bool loaded = false;
        int navigateFrames = 0;
        ed::EditorContext *editor = nullptr;
        std::map<int, ImVec2> pins;
    };

    Preview &preview()
    {
        static Preview p;
        return p;
    }

    std::string str(const Json &_j, const char *_key)
    {
        auto it = _j.find(_key);
        return it != _j.end() && it->is_string() ? it->get<std::string>() : std::string();
    }

    void reload(Preview &_p, const fs::path &_file, const std::string &_path)
    {
        std::error_code ec;
        const fs::file_time_type mtime = fs::last_write_time(_file, ec);
        if (_p.path == _path && !ec && mtime == _p.mtime)
            return;
        if (_p.path != _path)
            _p.navigateFrames = 3;
        _p.path = _path;
        _p.mtime = ec ? fs::file_time_type{} : mtime;
        std::ifstream in(_file);
        _p.doc = Json::parse(in, nullptr, false);
        _p.loaded = _p.doc.is_object() && _p.doc.contains("states") && _p.doc["states"].is_array() &&
                    _p.doc.contains("transitions") && _p.doc["transitions"].is_array();
    }

    bool writeDoc(Preview &_p, const fs::path &_file)
    {
        std::ofstream out(_file, std::ios::binary | std::ios::trunc);
        if (!(out << _p.doc.dump(4) << "\n"))
            return false;
        out.close();
        std::error_code ec;
        _p.mtime = fs::last_write_time(_file, ec);
        return true;
    }

    const ee::ipc::FsmLiveState *liveFor(App &_app, int _entity)
    {
        for (const ee::ipc::FsmLiveState &f : _app.game().fsmStates())
            if (f.entity == _entity)
                return &f;
        return nullptr;
    }

    void drawGraphPreview(Preview &_p, const std::string &_liveState)
    {
        if (!_p.editor)
        {
            ed::Config cfg;
            cfg.SettingsName = nullptr;
            _p.editor = ed::CreateEditor(&cfg);
        }
        ImGui::BeginChild("##fsm_preview", ImVec2(0.0f, 220.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ed::SetCurrentEditor(_p.editor);
        ed::Begin("##fsm_preview_canvas");
        fsmgraph::DrawOptions opt;
        opt.applyPositions = true;
        opt.liveState = _liveState;
        fsmgraph::drawNodesAndLinks(_p.doc, opt, _p.pins);
        if (_p.navigateFrames > 0 && --_p.navigateFrames == 0)
            ed::NavigateToContent(0.0f);
        ed::End();
        ed::SetCurrentEditor(nullptr);
        ImGui::EndChild();
    }

    bool drawDefault(Json &_param)
    {
        const std::string type = str(_param, "type");
        if (type == "trigger")
        {
            ImGui::TextDisabled("false");
            return false;
        }
        if (type == "bool")
        {
            bool v = _param.contains("default") && _param["default"].is_boolean() && _param["default"].get<bool>();
            if (!ImGui::Checkbox("##v", &v))
                return false;
            _param["default"] = v;
            return true;
        }
        if (type == "int")
        {
            int v = _param.contains("default") && _param["default"].is_number() ? _param["default"].get<int>() : 0;
            ImGui::DragInt("##v", &v, 0.1f);
            if (ImGui::IsItemEdited())
                _param["default"] = v;
            return ImGui::IsItemDeactivatedAfterEdit();
        }
        if (type == "float")
        {
            float v = _param.contains("default") && _param["default"].is_number() ? _param["default"].get<float>() : 0.0f;
            ImGui::DragFloat("##v", &v, 0.01f, 0.0f, 0.0f, "%.3f");
            if (ImGui::IsItemEdited())
                _param["default"] = ee::reflection::shortestFloat(v);
            return ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::TextDisabled("?");
        return false;
    }

    void drawLive(App &_app, int _entity, const std::string &_name, const std::string &_type, const Json &_params)
    {
        const Json *v = _params.contains(_name) ? &_params[_name] : nullptr;
        if (_type == "trigger")
        {
            const bool armed = v && v->is_boolean() && v->get<bool>();
            ImGui::TextColored(armed ? fsmgraph::kLiveColor : ImVec4(0.6f, 0.6f, 0.6f, 1.0f), armed ? "arme" : "-");
            ImGui::SameLine();
            if (ImGui::SmallButton("Declencher"))
                _app.game().sendFsmSet(_entity, _name, true);
            if (armed)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Vider"))
                    _app.game().sendFsmSet(_entity, _name, false);
            }
            return;
        }
        if (_type == "bool")
        {
            bool b = v && v->is_boolean() && v->get<bool>();
            if (ImGui::Checkbox("##v", &b))
                _app.game().sendFsmSet(_entity, _name, b);
            return;
        }
        if (_type == "int")
        {
            int i = v && v->is_number() ? v->get<int>() : 0;
            if (ImGui::DragInt("##v", &i, 0.1f))
                _app.game().sendFsmSet(_entity, _name, i);
            return;
        }
        if (_type == "float")
        {
            float f = v && v->is_number() ? v->get<float>() : 0.0f;
            if (ImGui::DragFloat("##v", &f, 0.01f, 0.0f, 0.0f, "%.3f"))
                _app.game().sendFsmSet(_entity, _name, ee::reflection::shortestFloat(f));
            return;
        }
        ImGui::TextDisabled("?");
    }
}

void drawStateMachineInspector(App &_app, Project *_project, const std::string &_path, int _entity, bool _playing)
{
    if (!_project || _path.empty())
    {
        ImGui::TextDisabled("Choisis un .fsm dans FSM Path");
        return;
    }
    ImGui::PushID("fsm_inspector");
    Preview &p = preview();
    const fs::path file = _project->root() / _path;
    reload(p, file, _path);

    if (ImGui::SmallButton("Ouvrir le graphe"))
        if (FsmPanel *panel = _app.findPanel<FsmPanel>())
            panel->open(_project, _path);

    const ee::ipc::FsmLiveState *live = _playing ? liveFor(_app, _entity) : nullptr;
    if (live)
    {
        ImGui::SameLine();
        ImGui::TextColored(fsmgraph::kLiveColor, "Etat : %s", live->state.empty() ? "(pas demarree)" : live->state.c_str());
    }

    if (!p.loaded)
    {
        ImGui::TextColored(fsmgraph::kErrorColor, "Fichier introuvable ou JSON invalide : %s", _path.c_str());
        ImGui::PopID();
        return;
    }

    drawGraphPreview(p, live ? live->state : std::string());

    ImGui::SeparatorText(_playing ? "Variables (en direct)" : "Variables (valeurs par defaut)");
    if (!p.doc.contains("parameters") || !p.doc["parameters"].is_array() || p.doc["parameters"].empty())
        ImGui::TextDisabled("Aucune variable");
    else if (_playing && !live)
        ImGui::TextDisabled("En attente du jeu...");
    else if (ImGui::BeginTable("##fsm_vars", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Nom", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("Valeur", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        bool changed = false;
        Json &params = p.doc["parameters"];
        for (size_t i = 0; i < params.size(); ++i)
        {
            Json &param = params[i];
            const std::string name = str(param, "name");
            const std::string type = str(param, "type");
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", type.c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (live)
                drawLive(_app, _entity, name, type, live->params);
            else
                changed |= drawDefault(param);
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (changed && !writeDoc(p, file))
            ImGui::TextColored(fsmgraph::kErrorColor, "Ecriture impossible : %s", _path.c_str());
    }
    ImGui::PopID();
}
