#pragma once

#include "visu/helpers/BuildProject.hpp"

#include "imgui.h"

#include <string>

// Barre de progression d'un build en cours + fichier compile ("Build : main.cpp").
// Pourcentage inconnu (configuration cmake...) : barre animee.
inline void drawBuildProgress(const projectbuild::AsyncBuild &_build, const char *_label)
{
    int pct = _build.percent();
    float fraction = pct >= 0 ? pct / 100.0f : -1.0f * (float)ImGui::GetTime();
    std::string overlay = pct >= 0 ? std::to_string(pct) + " %" : "";
    ImGui::ProgressBar(fraction, ImVec2(220.0f, 0.0f), overlay.empty() ? nullptr : overlay.c_str());
    ImGui::SameLine();
    std::string step = _build.step();
    ImGui::TextDisabled("%s%s", _label, step.empty() ? "..." : (" : " + step).c_str());
}
