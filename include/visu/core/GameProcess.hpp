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

class GameProcess
{
public:
    enum class State
    {
        Idle,
        Starting,
        Running,
        Crashed
    };

    GameProcess() = default;
    ~GameProcess();
    GameProcess(const GameProcess &) = delete;
    GameProcess &operator=(const GameProcess &) = delete;

    bool start(const std::filesystem::path &_exe, const std::filesystem::path &_logPath,
               ee::ipc::PlayMode _mode, std::string &_error);

    void update();
    void stop();
    void releaseGl();

    State state() const { return m_state; }
    bool running() const { return m_state == State::Starting || m_state == State::Running; }
    ee::ipc::PlayMode mode() const { return m_mode; }

    double secondsSinceStart() const
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startTime)
            .count();
    }

    unsigned int texture() const { return m_hasFrame ? m_texture : 0; }

    const SceneInfo *snapshot() const { return m_snapshot ? &*m_snapshot : nullptr; }
    uint64_t snapshotVersion() const { return m_snapshotVersion; }

    void setPaused(bool _paused);
    bool paused() const { return m_paused; }

    void sendSelect(int _entity);

    void sendResize(int _w, int _h);
    void sendInput(const ee::ipc::InputState &_in);
    void sendSetField(int _entity, const std::string &_comp, const std::string &_field,
                      const FieldValue &_value);

    const std::string &crashMessage() const { return m_crashMessage; }
    std::string log() const;

    void acknowledgeCrash();

private:
    bool processExited(bool &_clean, std::string &_how);
    void kill();
    void cleanup();
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
    bool m_hasFrame = false;
    int m_texW = 0, m_texH = 0;
    int m_sentW = 0, m_sentH = 0;
    bool m_paused = false;
    int m_sentSelect = -2; // -2 = jamais envoye

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
