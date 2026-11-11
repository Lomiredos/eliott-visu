#pragma once

#include "imgui.h"
#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace fsmgraph
{
    using Json = nlohmann::ordered_json;
    using ErrorMap = std::map<int, std::vector<std::string>>;

    inline constexpr unsigned long long kAnyNode = 1;
    inline constexpr unsigned long long kAnyOut = 2;
    inline constexpr unsigned long long kNodeBase = 10;
    inline constexpr unsigned long long kPinBase = 100000;
    inline constexpr unsigned long long kLinkBase = 1000000;
    inline constexpr int kAnyIndex = -2;

    inline const ImVec4 kAnyColor(1.0f, 0.65f, 0.25f, 1.0f);
    inline const ImVec4 kEntryColor(0.35f, 0.85f, 0.45f, 1.0f);
    inline const ImVec4 kLiveColor(0.3f, 0.65f, 1.0f, 1.0f);
    inline const ImVec4 kErrorColor(1.0f, 0.4f, 0.4f, 1.0f);

    unsigned long long nodeId(int _state);
    unsigned long long inPin(int _state);
    unsigned long long outPin(int _state);
    unsigned long long linkId(int _transition);
    int stateOfNode(unsigned long long _id);
    int stateOfPin(unsigned long long _id, bool &_isOut);
    int transitionOfLink(unsigned long long _id);

    ImVec2 nodePosition(const Json &_doc, int _state);

    struct DrawOptions
    {
        bool applyPositions = false;
        std::string liveState;
        const ErrorMap *stateErrors = nullptr;
        const ErrorMap *transitionErrors = nullptr;
    };

    void drawNodesAndLinks(const Json &_doc, const DrawOptions &_opt, std::map<int, ImVec2> &_outPins);
}
