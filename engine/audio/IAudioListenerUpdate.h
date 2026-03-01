#pragma once

/**
 * @file IAudioListenerUpdate.h
 * @brief Listener update interface for 3D audio (Engine calls each frame). M17.4.
 */

namespace engine::audio {

/**
 * @brief Interface for updating the audio listener each frame (position, velocity, orientation).
 * Implemented by AudioEngine; Engine calls it from Update() when set.
 */
class IAudioListenerUpdate {
public:
    virtual ~IAudioListenerUpdate() = default;

    /**
     * @brief Sets the listener state for 3D spatialisation.
     * @param position World position [x, y, z].
     * @param velocity [vx, vy, vz] (for doppler; can be zero).
     * @param forward  Unit forward vector [x, y, z].
     * @param up       Unit up vector [x, y, z].
     */
    virtual void SetListener(const float position[3], const float velocity[3],
                             const float forward[3], const float up[3]) = 0;
};

} // namespace engine::audio
