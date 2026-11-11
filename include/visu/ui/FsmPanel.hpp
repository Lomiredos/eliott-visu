#pragma once

#include "visu/ui/Panel.hpp"

#include "imgui.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace ax::NodeEditor
{
    struct EditorContext;
}

class FsmPanel : public Panel
{
public:
    ~FsmPanel() override;

    const char *name() const override { return "State Machines"; }
    void draw(Project *_project) override;
    void open(Project *_project, const std::string &_path);

private:
    using Json = nlohmann::ordered_json;

    enum class Sel
    {
        None,
        State,
        Any,
        Transition
    };

    void drawList(Project *_project);
    void drawTopBar(Project *_project);
    void drawGraph();
    void drawGraphNodes();
    void handleGraphInput();
    void drawGraphPopups();
    void drawInspector();
    void drawInspectorNone();
    void drawInspectorState(int _index);
    void drawInspectorAny();
    void drawInspectorTransition(int _index);
    void drawTransitionList(const std::string &_from);
    void drawValidation();
    void drawGeneral();
    void drawParameters();
    bool drawCondition(Json &_cond);
    bool drawAction(Json &_action);
    bool drawValue(const std::string &_label, Json &_value);
    bool drawAddField(Json &_action);
    bool editName(const char *_label, const std::string &_current, std::string &_out);

    void select(Project *_project, const std::string &_path);
    void close(Project *_project);
    void save(Project *_project);
    void refresh(Project *_project, bool _force = false);
    void rebuildTemplates(Project *_project);
    void normalize();
    void validate();
    void mapErrors();
    void undo();
    void redo();
    void updateLiveState();
    void changed();
    void structureChanged();

    void setSelection(Sel _kind, int _index);
    void startLinking(int _state);
    void onEditorSelection(const std::vector<unsigned long long> &_ids);
    int addState(const ImVec2 &_pos);
    void addTransition(const std::string &_from, const std::string &_to);
    void renameState(const std::string &_old, const std::string &_new);
    void renameParam(const std::string &_old, const std::string &_new);
    void removeState(int _index);
    void removeTransition(int _index);
    void moveTransition(int _index, int _dir);
    std::vector<std::string> stateNames() const;
    std::vector<std::string> paramNames() const;
    std::string paramType(const std::string &_name) const;
    std::string transitionSummary(const Json &_t) const;
    ImVec2 nodePosition(int _state) const;
    std::filesystem::file_time_type selectedMtime(Project *_project) const;

    ax::NodeEditor::EditorContext *m_editor = nullptr;

    std::string m_selected;
    Json m_doc;
    bool m_parsed = false;
    bool m_dirty = false;
    bool m_exists = false;
    std::filesystem::file_time_type m_mtime{};
    std::vector<std::string> m_files;
    std::chrono::steady_clock::time_point m_lastRefresh{};
    std::map<std::string, Json> m_actionTemplates;
    std::vector<std::string> m_errors;
    std::vector<std::string> m_warnings;
    std::vector<std::string> m_unknownTypes;
    std::map<int, std::vector<std::string>> m_stateErrors;
    std::map<int, std::vector<std::string>> m_transitionErrors;
    std::map<int, std::vector<std::string>> m_paramErrors;
    std::vector<Json> m_undo;
    std::vector<Json> m_redo;
    Json m_committed;
    bool m_readOnly = false;
    std::string m_liveState;
    int m_liveEntity = -1;

    Sel m_sel = Sel::None;
    int m_selIndex = -1;
    bool m_syncSelection = false;
    bool m_applyPositions = false;
    int m_navigateFrames = 0;
    bool m_linking = false;
    int m_linkFrom = -1;
    std::map<int, ImVec2> m_outPins;
    bool m_graphHovered = false;
    std::vector<unsigned long long> m_lastSelection;
    ImVec2 m_popupPos{};
    unsigned long long m_ctxNode = 0;
    unsigned long long m_ctxLink = 0;
    bool m_ignoreContext = false;

    std::string m_newName;
    std::string m_newActionType;
    std::string m_newFieldName;
    std::string m_status;
    unsigned int m_editId = 0;
    std::string m_editBuf;
};
