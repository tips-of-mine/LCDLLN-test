#pragma once

/**
 * @file HlodRuntime.h
 * @brief Runtime HLOD switching and CPU culling (M09.5).
 *
 * Seuil HLOD ~200m : au-delà on affiche HLOD au lieu des props individuels.
 * Culling frustum + distance (stable CPU).
 */

#include "engine/math/Frustum.h"

namespace engine::world {

/**
 * @brief Returns the distance threshold in meters beyond which HLOD is used instead of individual props.
 *
 * Config key: world.hlod_distance_threshold. Default 200.f.
 *
 * @return Distance in meters.
 */
float GetHlodDistanceThreshold();

/**
 * @brief Returns true if the given distance from camera should use HLOD (merged mesh) instead of instances.
 *
 * @param distance Distance from camera to chunk/object center in meters.
 * @return true to use HLOD, false to use individual props/instances.
 */
bool UseHlodForDistance(float distance);

/**
 * @brief Frustum culling: returns true if the AABB is visible (inside or intersecting the frustum).
 *
 * Stable CPU culling using the existing frustum planes.
 *
 * @param frustum View frustum (from view-projection).
 * @param aabbMin AABB minimum corner (world space), 3 floats.
 * @param aabbMax AABB maximum corner (world space), 3 floats.
 * @return true if visible, false if fully outside.
 */
bool VisibleInFrustum(const ::engine::math::Frustum& frustum, const float aabbMin[3], const float aabbMax[3]);

/**
 * @brief Distance culling: returns true if the point is within maxDistance of the camera.
 *
 * @param cameraPos Camera position (world), 3 floats.
 * @param point     Point to test (e.g. chunk center), 3 floats.
 * @param maxDistance Maximum draw distance in meters.
 * @return true if within range (should draw), false if beyond (cull).
 */
bool WithinDrawDistance(const float cameraPos[3], const float point[3], float maxDistance);

} // namespace engine::world
