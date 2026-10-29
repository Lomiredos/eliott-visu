#pragma once

#include "visu/core/SceneInfo.hpp"

#include <cstdint>
#include <deque>

// ---------------------------------------------------------------------------
// Historique Ctrl+Z / Ctrl+Y de la scene, par snapshots (SceneInfo complet).
//
// Les modifications marquent l'historique (touch). En fin de frame, commit()
// enregistre l'etat d'avant SAUF si un widget est encore manipule (slider,
// gizmo, saisie de texte) : un drag complet = une seule entree.
// ---------------------------------------------------------------------------
class SceneHistory
{
public:
    // Nouvelle scene chargee : historique vide, etat de reference = sauvegarde.
    void reset(const SceneInfo &_scene);

    // Une modification vient d'avoir lieu (appele par markSceneDirty).
    void touch() { m_pending = true; }

    // Fin de frame : enregistre la modification en cours si elle est terminee.
    // _selected = entite concernee, re-selectionnee a l'undo/redo.
    void commit(const SceneInfo &_scene, int _selected, bool _editing);

    bool canUndo() const { return !m_undo.empty() && !m_pending; }
    bool canRedo() const { return !m_redo.empty() && !m_pending; }
    bool undo(SceneInfo &_scene, int &_selected);
    bool redo(SceneInfo &_scene, int &_selected);

    // L'etat courant est celui ecrit sur le disque.
    void markSaved() { m_savedId = m_pending ? 0 : m_currentId; }
    bool atSaved() const { return !m_pending && m_currentId == m_savedId; }

private:
    struct Entry
    {
        SceneInfo scene;
        uint64_t id = 0;
        int selected = -1;
    };

    static constexpr size_t kMaxEntries = 100;

    std::deque<Entry> m_undo, m_redo;
    SceneInfo m_current; // dernier etat enregistre (= scene hors modif en cours)
    uint64_t m_currentId = 1;
    uint64_t m_savedId = 1;
    uint64_t m_nextId = 2;
    bool m_pending = false;
};
