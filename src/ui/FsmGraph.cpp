#include "visu/ui/FsmGraph.hpp"

#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace
{
    std::string str(const fsmgraph::Json &_j, const char *_key)
    {
        auto it = _j.find(_key);
        return it != _j.end() && it->is_string() ? it->get<std::string>() : std::string();
    }

    ImVec2 pinAnchor()
    {
        return ImVec2(ImGui::GetItemRectMax().x, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
    }
}

namespace fsmgraph
{
    unsigned long long nodeId(int _state) { return kNodeBase + static_cast<unsigned long long>(_state); }
    unsigned long long inPin(int _state) { return kPinBase + 2ull * static_cast<unsigned long long>(_state); }
    unsigned long long outPin(int _state) { return kPinBase + 2ull * static_cast<unsigned long long>(_state) + 1ull; }
    unsigned long long linkId(int _transition) { return kLinkBase + static_cast<unsigned long long>(_transition); }

    int stateOfNode(unsigned long long _id)
    {
        if (_id == kAnyNode)
            return kAnyIndex;
        return _id >= kNodeBase && _id < kPinBase ? static_cast<int>(_id - kNodeBase) : -1;
    }

    int stateOfPin(unsigned long long _id, bool &_isOut)
    {
        if (_id == kAnyOut)
        {
            _isOut = true;
            return kAnyIndex;
        }
        if (_id < kPinBase || _id >= kLinkBase)
            return -1;
        _isOut = (_id - kPinBase) % 2 == 1;
        return static_cast<int>((_id - kPinBase) / 2);
    }

    int transitionOfLink(unsigned long long _id)
    {
        return _id >= kLinkBase ? static_cast<int>(_id - kLinkBase) : -1;
    }

    ImVec2 nodePosition(const Json &_doc, int _state)
    {
        if (_state == kAnyIndex)
        {
            if (_doc.contains("editor") && _doc["editor"].contains("any"))
            {
                const Json &p = _doc["editor"]["any"];
                if (p.contains("x") && p["x"].is_number() && p.contains("y") && p["y"].is_number())
                    return ImVec2(p["x"].get<float>(), p["y"].get<float>());
            }
            return ImVec2(-260.0f, 0.0f);
        }
        const Json &s = _doc["states"][_state];
        if (s.contains("editor") && s["editor"].is_object())
        {
            const Json &e = s["editor"];
            if (e.contains("x") && e["x"].is_number() && e.contains("y") && e["y"].is_number())
                return ImVec2(e["x"].get<float>(), e["y"].get<float>());
        }
        return ImVec2(220.0f * static_cast<float>(_state), 0.0f);
    }

    void drawNodesAndLinks(const Json &_doc, const DrawOptions &_opt, std::map<int, ImVec2> &_outPins)
    {
        const Json &states = _doc["states"];
        const std::string entry = str(_doc, "entryState");
        std::map<std::string, int> indexOf;
        for (size_t i = 0; i < states.size(); ++i)
            indexOf[str(states[i], "name")] = static_cast<int>(i);
        _outPins.clear();

        if (_opt.applyPositions)
            ed::SetNodePosition(kAnyNode, nodePosition(_doc, kAnyIndex));
        ed::PushStyleColor(ed::StyleColor_NodeBorder, kAnyColor);
        ed::BeginNode(kAnyNode);
        ImGui::TextColored(kAnyColor, "Any");
        ImGui::SameLine();
        ed::BeginPin(kAnyOut, ed::PinKind::Output);
        ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
        ImGui::TextUnformatted(">");
        _outPins[kAnyIndex] = pinAnchor();
        ed::EndPin();
        ImGui::TextDisabled("depuis tout etat");
        ed::EndNode();
        ed::PopStyleColor();

        for (size_t i = 0; i < states.size(); ++i)
        {
            const int s = static_cast<int>(i);
            const std::string name = str(states[i], "name");
            const bool isEntry = name == entry;
            const auto err = _opt.stateErrors ? _opt.stateErrors->find(s) : ErrorMap::const_iterator{};
            const bool hasError = _opt.stateErrors && err != _opt.stateErrors->end();
            const bool isLive = !_opt.liveState.empty() && name == _opt.liveState;
            if (_opt.applyPositions)
                ed::SetNodePosition(nodeId(s), nodePosition(_doc, s));

            int colors = 0;
            if (hasError || isEntry || isLive)
            {
                ed::PushStyleColor(ed::StyleColor_NodeBorder, hasError ? kErrorColor : isLive ? kLiveColor : kEntryColor);
                ++colors;
            }
            if (isLive)
            {
                ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.12f, 0.25f, 0.45f, 0.95f));
                ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 3.0f);
                ++colors;
            }

            ed::BeginNode(nodeId(s));
            ed::BeginPin(inPin(s), ed::PinKind::Input);
            ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
            ImGui::TextUnformatted(">");
            ed::EndPin();
            ImGui::SameLine();
            ImGui::TextUnformatted(name.c_str());
            ImGui::SameLine();
            ed::BeginPin(outPin(s), ed::PinKind::Output);
            ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
            ImGui::TextUnformatted(">");
            _outPins[s] = pinAnchor();
            ed::EndPin();
            if (isEntry)
                ImGui::TextColored(kEntryColor, "entree");
            if (isLive)
                ImGui::TextColored(kLiveColor, "en cours");
            if (hasError)
                ImGui::TextColored(kErrorColor, "%zu erreur(s)", err->second.size());
            if (states[i].contains("actions"))
                for (const Json &a : states[i]["actions"])
                    ImGui::TextDisabled("- %s", str(a, "type").c_str());
            ed::EndNode();

            if (isLive)
                ed::PopStyleVar();
            if (colors)
                ed::PopStyleColor(colors);
        }

        const Json &tr = _doc["transitions"];
        for (size_t t = 0; t < tr.size(); ++t)
        {
            const std::string from = str(tr[t], "from");
            auto to = indexOf.find(str(tr[t], "to"));
            if (to == indexOf.end())
                continue;
            unsigned long long start = 0;
            if (from == "Any")
                start = kAnyOut;
            else if (auto f = indexOf.find(from); f != indexOf.end())
                start = outPin(f->second);
            else
                continue;
            const bool hasError = _opt.transitionErrors && _opt.transitionErrors->count(static_cast<int>(t));
            ed::Link(linkId(static_cast<int>(t)), start, inPin(to->second),
                     hasError ? kErrorColor : from == "Any" ? kAnyColor : ImVec4(0.85f, 0.85f, 0.9f, 1.0f), 2.0f);
        }
    }
}
