#pragma once

#include <string>

class App;
class Project;

void drawStateMachineInspector(App &_app, Project *_project, const std::string &_path, int _entity, bool _playing);
