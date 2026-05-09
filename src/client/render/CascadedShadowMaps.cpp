#include "engine/render/CascadedShadowMaps.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace engine::render
{
	namespace
	{
		/// Builds an orthographic projection matrix with Vulkan-style depth (Z in [0,1])
		/// and Y-down NDC, matching the conventions used by PerspectiveVulkan.
		///
		/// \param left   Left plane in view space.
		/// \param right  Right plane in view space.
		/// \param bottom Bottom plane in view space.
		/// \param top    Top plane in view space.
		/// \param nearZ  Near plane distance.
		/// \param farZ   Far plane distance.
		engine::math::Mat4 OrthoVulkan(float left, float right,
			float bottom, float top,
			float nearZ, float farZ)
		{
			using engine::math::Mat4;

			const float rl = right - left;
			const float tb = top - bottom;
			const float fn = farZ - nearZ;

			Mat4 out;
			out.m[0] = (rl != 0.0f) ? (2.0f / rl) : 0.0f;
			out.m[1] = 0.0f;
			out.m[2] = 0.0f;
			out.m[3] = 0.0f;

			out.m[4] = 0.0f;
			// Y-down in NDC: negative scale on Y.
			out.m[5] = (tb != 0.0f) ? (-2.0f / tb) : 0.0f;
			out.m[6] = 0.0f;
			out.m[7] = 0.0f;

			out.m[8]  = 0.0f;
			out.m[9]  = 0.0f;
			out.m[10] = (fn != 0.0f) ? (1.0f / fn) : 0.0f;
			out.m[11] = 0.0f;

			out.m[12] = (rl != 0.0f) ? (-(right + left) / rl) : 0.0f;
			out.m[13] = (tb != 0.0f) ? (-(top + bottom) / tb) : 0.0f;
			out.m[14] = (fn != 0.0f) ? (-nearZ / fn) : 0.0f;
			out.m[15] = 1.0f;

			return out;
		}

		/// Builds a light view matrix for a directional light.
		/// The matrix transforms from world space to light space.
		engine::math::Mat4 BuildLightView(const engine::math::Vec3& lightPosition,
			const engine::math::Vec3& lightForward)
		{
			using engine::math::Mat4;
			using engine::math::Vec3;

			// Ensure forward is normalized and points from light toward the scene.
			const Vec3 f = lightForward.Normalized();

			// Choose an arbitrary world up, fall back if nearly parallel to forward.
			Vec3 worldUp(0.0f, 1.0f, 0.0f);
			const float dotUp = f.x * worldUp.x + f.y * worldUp.y + f.z * worldUp.z;
			if (std::fabs(dotUp) > 0.99f)
			{
				worldUp = Vec3(1.0f, 0.0f, 0.0f);
			}

			// Right = normalize(cross(up, forward)).
			Vec3 right(
				worldUp.y * f.z - worldUp.z * f.y,
				worldUp.z * f.x - worldUp.x * f.z,
				worldUp.x * f.y - worldUp.y * f.x);
			const float rLen = right.Length();
			if (rLen > 0.0f)
			{
				right = right * (1.0f / rLen);
			}
			else
			{
				right = Vec3(1.0f, 0.0f, 0.0f);
			}

			// Recompute orthogonal up = cross(forward, right).
			Vec3 up(
				f.y * right.z - f.z * right.y,
				f.z * right.x - f.x * right.z,
				f.x * right.y - f.y * right.x);
			const float uLen = up.Length();
			if (uLen > 0.0f)
			{
				up = up * (1.0f / uLen);
			}
			else
			{
				up = Vec3(0.0f, 1.0f, 0.0f);
			}

			Mat4 V;
			V.m[0] = right.x;  V.m[1] = right.y;  V.m[2]  = right.z;  V.m[3]  = 0.0f;
			V.m[4] = up.x;     V.m[5] = up.y;     V.m[6]  = up.z;     V.m[7]  = 0.0f;
			V.m[8] = -f.x;     V.m[9] = -f.y;     V.m[10] = -f.z;     V.m[11] = 0.0f;
			V.m[12] = -(right.x * lightPosition.x + right.y * lightPosition.y + right.z * lightPosition.z);
			V.m[13] = -(up.x * lightPosition.x + up.y * lightPosition.y + up.z * lightPosition.z);
			V.m[14] =  (f.x * lightPosition.x + f.y * lightPosition.y + f.z * lightPosition.z);
			V.m[15] = 1.0f;
			return V;
		}
	}

	void ComputePracticalSplits(float nearZ, float farZ, float lambda, float(&outSplitDepths)[kCascadeCount])
	{
		const float n = std::max(nearZ, 0.0001f);
		const float f = std::max(farZ, n + 0.0001f);
		const float invCascadeCount = 1.0f / static_cast<float>(kCascadeCount);

		for (uint32_t i = 0; i < kCascadeCount; ++i)
		{
			const float si = static_cast<float>(i + 1) * invCascadeCount;

			const float uniSplit = n + (f - n) * si;
			const float logSplit = n * std::pow(f / n, si);
			const float split = lambda * logSplit + (1.0f - lambda) * uniSplit;
			outSplitDepths[i] = split;
		}

		// Ensure strictly increasing order (numerical safety).
		for (uint32_t i = 1; i < kCascadeCount; ++i)
		{
			if (outSplitDepths[i] <= outSplitDepths[i - 1])
			{
				outSplitDepths[i] = outSplitDepths[i - 1] + 0.001f;
			}
		}
	}

	void ComputeCascades(const Camera& camera,
		const engine::math::Vec3& lightDirTowardLight,
		float lambda,
		uint32_t shadowMapResolution,
		CascadesUniform& outCascades)
	{
		using engine::math::Mat4;
		using engine::math::Vec3;

		const float invShadowRes = (shadowMapResolution > 0u)
			? (1.0f / static_cast<float>(shadowMapResolution))
			: 1.0f;

		// 1) Compute split distances in view space.
		float splits[kCascadeCount]{};
		ComputePracticalSplits(camera.nearZ, camera.farZ, lambda, splits);
		for (uint32_t i = 0; i < kCascadeCount; ++i)
		{
			outCascades.splitDepths[i] = splits[i];
		}

		// 2) Build camera basis (forward/right/up) matching Camera::ComputeViewMatrix.
		const float cy = std::cos(camera.yaw);
		const float sy = std::sin(camera.yaw);
		const float cp = std::cos(camera.pitch);
		const float sp = std::sin(camera.pitch);

		const Vec3 forward(-sy * cp, -sp, -cy * cp);

		Vec3 right(forward.z, 0.0f, -forward.x);
		float rlen = right.Length();
		if (rlen > 0.0f)
			right = right * (1.0f / rlen);
		else
			right = Vec3(1.0f, 0.0f, 0.0f);

		Vec3 up(
			forward.y * right.z - forward.z * right.y,
			forward.z * right.x - forward.x * right.z,
			forward.x * right.y - forward.y * right.x);
		float ulen = up.Length();
		if (ulen > 0.0f)
			up = up * (1.0f / ulen);
		else
			up = Vec3(0.0f, 1.0f, 0.0f);

		const Vec3 camPos = camera.position;

		const float fovYRad = camera.fovYDeg * 3.14159265f / 180.0f;
		const float tanHalfFovY = std::tan(fovYRad * 0.5f);

		// Light forward points from light toward the scene.
		const Vec3 lightForward = (lightDirTowardLight * -1.0f).Normalized();

		for (uint32_t c = 0; c < kCascadeCount; ++c)
		{
			const float prevSplit = (c == 0) ? camera.nearZ : splits[c - 1];
			const float currSplit = splits[c];

			// 3) Compute view-space sizes for this cascade slice.
			const float nearDist = prevSplit;
			const float farDist = currSplit;

			const float nearHeight = 2.0f * nearDist * tanHalfFovY;
			const float nearWidth = nearHeight * camera.aspect;

			const float farHeight = 2.0f * farDist * tanHalfFovY;
			const float farWidth = farHeight * camera.aspect;

			// 4) Compute world-space frustum slice corners.
			const Vec3 centerNear = camPos + forward * nearDist;
			const Vec3 centerFar = camPos + forward * farDist;

			const Vec3 nearRight = right * (nearWidth * 0.5f);
			const Vec3 nearUp = up * (nearHeight * 0.5f);
			const Vec3 farRight = right * (farWidth * 0.5f);
			const Vec3 farUp = up * (farHeight * 0.5f);

			std::array<Vec3, 8> corners{};
			// Near plane (0..3).
			corners[0] = centerNear - nearRight - nearUp;
			corners[1] = centerNear + nearRight - nearUp;
			corners[2] = centerNear + nearRight + nearUp;
			corners[3] = centerNear - nearRight + nearUp;
			// Far plane (4..7).
			corners[4] = centerFar - farRight - farUp;
			corners[5] = centerFar + farRight - farUp;
			corners[6] = centerFar + farRight + farUp;
			corners[7] = centerFar - farRight + farUp;

			// 5) Compute slice center and approximate radius for light placement.
			Vec3 sliceCenter(0.0f, 0.0f, 0.0f);
			for (const Vec3& v : corners)
			{
				sliceCenter.x += v.x;
				sliceCenter.y += v.y;
				sliceCenter.z += v.z;
			}
			sliceCenter = sliceCenter * (1.0f / 8.0f);

			float radius = 0.0f;
			for (const Vec3& v : corners)
			{
				const float dx = v.x - sliceCenter.x;
				const float dy = v.y - sliceCenter.y;
				const float dz = v.z - sliceCenter.z;
				const float distSq = dx * dx + dy * dy + dz * dz;
				radius = std::max(radius, std::sqrt(distSq));
			}

			// Place the directional light far enough along its forward direction.
			const float backoff = radius * 2.0f;
			const Vec3 lightPos(
				sliceCenter.x - lightForward.x * backoff,
				sliceCenter.y - lightForward.y * backoff,
				sliceCenter.z - lightForward.z * backoff);

			const Mat4 lightView = BuildLightView(lightPos, lightForward);

			// 6) Transform corners into light space to build an AABB.
			float minX = std::numeric_limits<float>::max();
			float minY = std::numeric_limits<float>::max();
			float minZ = std::numeric_limits<float>::max();
			float maxX = std::numeric_limits<float>::lowest();
			float maxY = std::numeric_limits<float>::lowest();
			float maxZ = std::numeric_limits<float>::lowest();

			for (const Vec3& v : corners)
			{
				const float x = v.x;
				const float y = v.y;
				const float z = v.z;

				const float lx = lightView.m[0] * x + lightView.m[4] * y + lightView.m[8]  * z + lightView.m[12];
				const float ly = lightView.m[1] * x + lightView.m[5] * y + lightView.m[9]  * z + lightView.m[13];
				const float lz = lightView.m[2] * x + lightView.m[6] * y + lightView.m[10] * z + lightView.m[14];

				minX = std::min(minX, lx);
				minY = std::min(minY, ly);
				minZ = std::min(minZ, lz);
				maxX = std::max(maxX, lx);
				maxY = std::max(maxY, ly);
				maxZ = std::max(maxZ, lz);
			}

			// 7) Stabilization: snap orthographic center to shadow-map texel grid in light space.
			// worldUnitsPerTexel = extent / shadowResolution so that one texel = one grid step.
			const float extentX = maxX - minX;
			const float extentY = maxY - minY;
			const float extent = std::max(extentX, extentY);
			const float worldUnitsPerTexel = (extent > 0.0f && shadowMapResolution > 0u)
				? (extent * invShadowRes)
				: 1.0f;

			const float centerX = 0.5f * (minX + maxX);
			const float centerY = 0.5f * (minY + maxY);

			const float invTex = 1.0f / worldUnitsPerTexel;
			const float snappedCenterX = std::floor(centerX * invTex + 0.5f) * worldUnitsPerTexel;
			const float snappedCenterY = std::floor(centerY * invTex + 0.5f) * worldUnitsPerTexel;

			const float offsetX = snappedCenterX - centerX;
			const float offsetY = snappedCenterY - centerY;

			minX += offsetX; maxX += offsetX;
			minY += offsetY; maxY += offsetY;

			// Slightly extend depth range to avoid clipping due to numerical imprecision.
			const float depthPadding = 5.0f;
			minZ -= depthPadding;
			maxZ += depthPadding;

			const Mat4 lightProj = OrthoVulkan(minX, maxX, minY, maxY, minZ, maxZ);
			outCascades.lightViewProj[c] = lightProj * lightView;
		}
	}
}

