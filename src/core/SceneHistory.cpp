#include "visu/core/SceneHistory.hpp"

#include <utility>

static int clampSelection(const SceneInfo &_scene, int _selected)
{
    return _selected >= 0 && _selected < static_cast<int>(_scene.entities.size()) ? _selected : -1;
}

void SceneHistory::reset(const SceneInfo &_scene, bool _saved)
{
    m_undo.clear();
    m_redo.clear();
    m_current = _scene;
    m_currentId = m_nextId++;
    m_savedId = _saved ? m_currentId : 0; // 0 : aucun etat ne correspond au disque
    m_pending = false;
}

void SceneHistory::commit(const SceneInfo &_scene, int _selected, bool _editing)
{
    if (!m_pending || _editing)
        return;
    m_pending = false;

    // Etat d'avant -> pile undo ; une nouvelle action efface le "futur".
    m_undo.push_back({std::move(m_current), m_currentId, _selected});
    if (m_undo.size() > kMaxEntries)
        m_undo.pop_front();
    m_redo.clear();

    m_current = _scene;
    m_currentId = m_nextId++;
}

bool SceneHistory::undo(SceneInfo &_scene, int &_selected)
{
    if (!canUndo())
        return false;
    Entry prev = std::move(m_undo.back());
    m_undo.pop_back();

    // L'etat courant part dans redo, avec la meme entite concernee.
    m_redo.push_back({std::move(m_current), m_currentId, prev.selected});
    m_current = std::move(prev.scene);
    m_currentId = prev.id;

    _scene = m_current;
    _selected = clampSelection(_scene, prev.selected);
    return true;
}

bool SceneHistory::redo(SceneInfo &_scene, int &_selected)
{
    if (!canRedo())
        return false;
    Entry next = std::move(m_redo.back());
    m_redo.pop_back();

    m_undo.push_back({std::move(m_current), m_currentId, next.selected});
    m_current = std::move(next.scene);
    m_currentId = next.id;

    _scene = m_current;
    _selected = clampSelection(_scene, next.selected);
    return true;
}
