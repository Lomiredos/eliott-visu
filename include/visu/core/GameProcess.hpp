#pragma once

#include "visu/core/SceneInfo.hpp"
#include "visu/ipc/Protocol.hpp"
#include "visu/ipc/SharedFrame.hpp"
#include "visu/ipc/Socket.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// ---------------------------------------------------------------------------
// Le jeu lance depuis l'editeur (bouton Play), dans un PROCESS SEPARE : un
// crash du jeu ne fait jamais tomber l'editeur.
//
// Lance l'executable avec EE_EDITOR_HOST (port + mode + memoire partagee),
// ecrit sa sortie dans un log, puis a chaque frame (update, non bloquant) :
// recoit les snapshots du World, recupere la derniere image (mode preview)
// et surveille la fin du process (sortie normale ou crash).
// ---------------------------------------------------------------------------
class GameProcess
{
public:
    enum class State
    {
        Idle,     // rien ne tourne
        Starting, // process lance, pas encore connecte
        Running,  // connecte
        Crashed   // fin anormale : voir crashMessage() / log()
    };

    GameProcess() = default;
    ~GameProcess();
    GameProcess(const GameProcess &) = delete;
    GameProcess &operator=(const GameProcess &) = delete;

    // Lance _exe (dossier courant = celui de l'exe), sortie dans _logPath.
    // false + _error si le lancement est impossible.
    bool start(const std::filesystem::path &_exe, const std::filesystem::path &_logPath,
               ee::ipc::PlayMode _mode, std::string &_error);
    // A appeler a chaque frame de l'editeur (avec le contexte GL courant).
    void update();
    // Arret demande : Stop au jeu, puis kill s'il ne sort pas vite.
    void stop();
    // Libere la texture GL (a appeler tant que le contexte GL existe).
    void releaseGl();

    State state() const { return m_state; }
    bool running() const { return m_state == State::Starting || m_state == State::Running; }
    ee::ipc::PlayMode mode() const { return m_mode; }
    // Secondes depuis le lancement (pour signaler un jeu qui ne se connecte pas).
    double secondsSinceStart() const
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startTime).count();
    }

    // Derniere image du jeu (mode preview) : texture GL RGBA, lignes de bas en
    // haut (afficher avec uv0=(0,1), uv1=(1,0)). 0 tant qu'aucune image.
    unsigned int texture() const { return m_hasFrame ? m_texture : 0; }

    // Dernier etat du World recu (meme index d'entite que la scene).
    const SceneInfo *snapshot() const { return m_snapshot ? &*m_snapshot : nullptr; }
    // Incremente a chaque nouveau snapshot.
    uint64_t snapshotVersion() const { return m_snapshotVersion; }

    void sendResize(int _w, int _h);
    void sendInput(const ee::ipc::InputState &_in);
    void sendSetField(int _entity, const std::string &_comp, const std::string &_field,
                      const FieldValue &_value);

    // Apres un crash : description (code / signal) et sortie du jeu.
    const std::string &crashMessage() const { return m_crashMessage; }
    std::string log() const;
    // L'utilisateur a vu le crash : retour a Idle.
    void acknowledgeCrash();

private:
    // Le process est-il fini ? Si oui : _clean (sortie 0) et _how (texte).
    bool processExited(bool &_clean, std::string &_how);
    void kill();
    void cleanup(); // sockets, memoire partagee, snapshot (pas la texture)
    void uploadFrame();

    State m_state = State::Idle;
    ee::ipc::PlayMode m_mode = ee::ipc::PlayMode::Preview;
    bool m_stopRequested = false;
    std::chrono::steady_clock::time_point m_startTime;

    ee::ipc::Socket m_listen;
    ee::ipc::Socket m_conn;
    ee::ipc::SharedFrame m_frame;
    uint64_t m_frameCounter = 0;

    unsigned int m_texture = 0;
    bool m_hasFrame = false; // une image de CETTE session a ete recue
    int m_texW = 0, m_texH = 0;
    int m_sentW = 0, m_sentH = 0;

    std::optional<SceneInfo> m_snapshot;
    uint64_t m_snapshotVersion = 0;

    std::filesystem::path m_logPath;
    std::string m_crashMessage;

#ifdef _WIN32
    void *m_process = nullptr; // HANDLE
#else
    int m_pid = -1;
#endif
};
