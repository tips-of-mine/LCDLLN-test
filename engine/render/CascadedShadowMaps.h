#pragma once

#include "engine/math/Math.h"
#include "engine/render/Camera.h"

namespace engine::render
{
	/// Number of cascades used for cascaded shadow mapping.
	inline constexpr uint32_t kCascadeCount = 4u;

	/// CPU-side representation of the cascades uniform buffer.
	/// This layout is intended to mirror the GPU uniform buffer `Cascades`
	/// that will be consumed by shadow and lighting shaders in later tickets.
	struct CascadesUniform
	{
		/// Light view-projection matrices for each cascade (proj * view, column-major).
		engine::math::Mat4 lightViewProj[kCascadeCount]{};
		/// View-space split distances (far plane for each cascade, in units of camera near/far).
		float splitDepths[kCascadeCount]{};
	};

	/// Computes practical split distances for cascaded shadow maps.
	///
	/// Splits are computed between \p nearZ and \p farZ using the "practical split" formula:
	///   split_i = lerp(uni_i, log_i, lambda)
	/// where uni_i is the uniform split and log_i the logarithmic split.
	/// The resulting distances are strictly increasing and stored in \p outSplitDepths.
	///
	/// \param nearZ            Camera near plane distance (> 0).
	/// \param farZ             Camera far plane distance (> nearZ).
	/// \param lambda           Blend factor between uniform (0) and logarithmic (1) splits.
	/// \param outSplitDepths   Output array of size kCascadeCount receiving split distances.
	void ComputePracticalSplits(float nearZ, float farZ, float lambda, float(&outSplitDepths)[kCascadeCount]);

	/// Computes cascaded shadow matrices and split distances for a directional light.
	///
	/// This uses the camera frustum defined by \p camera (position, orientation,
	/// FOV, aspect, near/far) and a world-space light direction pointing
	/// TOWARD the light source. For each cascade:
	///   - A frustum slice is defined between consecutive split distances.
	///   - The slice is enclosed in a tight axis-aligned bounding box in light space.
	///   - An orthographic projection is built from that AABB.
	///   - The orthographic volume is stabilized by snapping its center in light space
	///     to a multiple of \p worldUnitsPerTexel.
	///
	/// The resulting matrices and split distances are written to \p outCascades.
	///
	/// \param camera                Camera defining the view frustum.
	/// \param lightDirTowardLight   Normalized world-space direction pointing toward the light.
	/// \param lambda                Practical split factor (recommended 0.7).
	/// \param worldUnitsPerTexel    World-space size of a shadow-map texel for stabilization (> 0).
	/// \param outCascades           Output cascades uniform data (matrices + split depths).
	void ComputeCascades(const Camera& camera,
		const engine::math::Vec3& lightDirTowardLight,
		float lambda,
		float worldUnitsPerTexel,
		CascadesUniform& outCascades);
}

