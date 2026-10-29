#pragma once

#include "visu/helpers/Directions.hpp"

class Project;

struct DockRequest
{
    unsigned int dockTarget = 0;
    unsigned int splitSource = 0;
    Dir splitDir;
    float splitRatio = 0.5f;
};

class Panel
{
public:
    virtual ~Panel() = default;

    virtual const char *name() const = 0;
    virtual void draw(Project *_project) = 0;
    // Appele a la place de draw() quand la fenetre existe mais n'est pas
    // affichee (onglet inactif) : sert a garder vivants les dockspaces internes.
    virtual void drawHidden() {}

    bool isVisible() const { return m_visible; }
    void setVisible(bool _visible) { m_visible = _visible; }
    bool isLocked() const { return m_locked; }
    bool removeOnClose() const { return m_removeOnClose; }
    DockRequest &dockRequest() { return m_dock; }

protected:
    bool m_locked = false;
    bool m_removeOnClose = false; // detruit quand ferme (ex: ShowCode), pas juste masque

private:
    DockRequest m_dock;
    bool m_visible = true;
};
