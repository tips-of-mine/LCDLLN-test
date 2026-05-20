#include "src/client/render/skinned/SkinnedMeshLoader.h"

// SkinnedMesh.h inclut SkinnedMeshLoader.h ; comme on l'inclut après le header
// du loader, le forward-decl `struct SkinnedMesh;` côté loader est déjà visible
// quand le compilateur voit la définition complète ici — pas de cycle.
#include "src/client/render/skinned/SkinnedMesh.h"

// Définit l'implémentation interne de cgltf (header-only stb-style). Ne doit
// être défini que dans UN seul .cpp du projet ; aucune autre TU n'inclut
// cgltf actuellement (vérifié à la création de Task 10).
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "src/shared/math/Quat.h"

#include "src/shared/core/Log.h"

#include <cstdio>
#include <cstring>
#include <functional>

namespace engine::render::skinned
{

namespace
{
    /// Lit toutes les keyframes d'un sampler cgltf vers un std::vector<Keyframe<T>>.
    ///
    /// \tparam T          Type de la valeur de keyframe (Vec3 pour T/S, Quat pour R).
    /// \tparam Components Nombre de composantes scalaires de T (3 pour Vec3, 4 pour Quat).
    /// \param  sampler    Sampler cgltf (input = times, output = values). Si null, renvoie vide.
    /// \param  ctor       Constructeur T à partir d'un float* (taille Components).
    /// \return Keyframes triées par t croissant (ordre glTF préservé).
    ///
    /// Notes :
    ///   - Pour les interpolations CUBICSPLINE, cgltf stocke (in-tangent, value, out-tangent)
    ///     consécutifs ; on lit seulement la value ici (Components à 3 ou 4 par keyframe),
    ///     ce qui équivaut à une interpolation LINEAR. Mixamo n'utilise pas CUBICSPLINE
    ///     pour les clips de marche, donc OK pour le MVP.
    ///   - On ignore les keyframes dont la lecture échoue (au lieu d'avorter), pour rester
    ///     robuste face à un asset partiellement corrompu.
    template <typename T, int Components>
    std::vector<Keyframe<T>> LoadKeyframes(const cgltf_animation_sampler* sampler,
                                            std::function<T(const float*)> ctor)
    {
        std::vector<Keyframe<T>> out;
        if (!sampler || !sampler->input || !sampler->output) return out;
        const cgltf_accessor* input = sampler->input;
        const cgltf_accessor* output = sampler->output;
        if (input->count != output->count) return out;
        out.reserve(input->count);
        for (cgltf_size i = 0; i < input->count; ++i) {
            float t = 0.0f;
            if (!cgltf_accessor_read_float(input, i, &t, 1)) continue;
            float vals[4] = {0, 0, 0, 0};
            if (!cgltf_accessor_read_float(output, i, vals, Components)) continue;
            out.push_back({t, ctor(vals)});
        }
        return out;
    }
}

std::optional<SkinnedMeshCpuData> SkinnedMeshLoader::LoadCpuOnlyForTests(const std::string& path)
{
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success) {
        LOG_WARN(Render, "[SkinnedMeshLoader] Parse failed for '{}' (cgltf result={})",
                     path, static_cast<int>(result));
        if (data) cgltf_free(data);
        return std::nullopt;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        LOG_WARN(Render, "[SkinnedMeshLoader] Buffer load failed for '{}'", path);
        cgltf_free(data);
        return std::nullopt;
    }

    SkinnedMeshCpuData out;

    if (data->skins_count == 0 || !data->skins) {
        LOG_WARN(Render, "[SkinnedMeshLoader] No skin in '{}'", path);
        cgltf_free(data);
        return std::nullopt;
    }
    const cgltf_skin* skin = &data->skins[0];

    // Construit le squelette à partir de skin->joints. On itère deux fois :
    // d'abord pour remplir les données de base (name, bindLocal, IBM), puis pour
    // résoudre les indices parents (qui requièrent que tous les joints soient présents).
    out.skeleton.bones.resize(skin->joints_count);
    for (cgltf_size i = 0; i < skin->joints_count; ++i) {
        const cgltf_node* joint = skin->joints[i];
        out.skeleton.bones[i].name = joint->name ? joint->name : ("bone_" + std::to_string(i));
        out.skeleton.bones[i].parentIndex = -1;
        if (joint->has_matrix) {
            std::memcpy(out.skeleton.bones[i].bindLocal.m, joint->matrix, sizeof(float) * 16);
        } else {
            // Compose TRS depuis les champs séparés du node glTF.
            engine::math::Vec3 t{joint->translation[0], joint->translation[1], joint->translation[2]};
            engine::math::Quat r{joint->rotation[0], joint->rotation[1], joint->rotation[2], joint->rotation[3]};
            engine::math::Vec3 s{joint->scale[0], joint->scale[1], joint->scale[2]};
            engine::math::Mat4 rot = r.ToMat4();
            // Applique le scale en multipliant chaque colonne (column-major : col k = m[k*4 .. k*4+3]).
            rot.m[0] *= s.x; rot.m[1] *= s.x; rot.m[2] *= s.x;
            rot.m[4] *= s.y; rot.m[5] *= s.y; rot.m[6] *= s.y;
            rot.m[8] *= s.z; rot.m[9] *= s.z; rot.m[10] *= s.z;
            // Translation dans la 4e colonne.
            rot.m[12] = t.x; rot.m[13] = t.y; rot.m[14] = t.z;
            out.skeleton.bones[i].bindLocal = rot;
        }
        if (skin->inverse_bind_matrices) {
            float ibm[16];
            cgltf_accessor_read_float(skin->inverse_bind_matrices, i, ibm, 16);
            std::memcpy(out.skeleton.bones[i].inverseBindGlobal.m, ibm, sizeof(float) * 16);
        }
    }
    /// Renvoie l'index dans skin->joints[] du node passé, ou -1 si introuvable.
    auto FindJointIndex = [&](const cgltf_node* n) -> int {
        for (cgltf_size i = 0; i < skin->joints_count; ++i) {
            if (skin->joints[i] == n) return static_cast<int>(i);
        }
        return -1;
    };
    for (cgltf_size i = 0; i < skin->joints_count; ++i) {
        const cgltf_node* joint = skin->joints[i];
        out.skeleton.bones[i].parentIndex = (joint->parent ? FindJointIndex(joint->parent) : -1);
    }

    // IMPORTANT — AnimationSampler::ComputeGlobalMatrices requiert l'invariant
    // bones[i].parentIndex < i. cgltf préserve l'ordre dans lequel les joints
    // apparaissent dans skin->joints[], que Mixamo exporte en ordre topologique
    // (racine d'abord, enfants ensuite). Si un glTF futur violait ça, il faudrait
    // un tri topologique ici. Pour Y Bot l'invariant est respecté.
    // On émet un warn (pas throw) si violé — le test attrape l'erreur tôt.
    for (size_t i = 0; i < out.skeleton.bones.size(); ++i) {
        if (out.skeleton.bones[i].parentIndex >= static_cast<int>(i)) {
            LOG_WARN(Render, "[SkinnedMeshLoader] Skeleton not in topological order: bone {} ('{}') parent={} >= self={}",
                         i, out.skeleton.bones[i].name, out.skeleton.bones[i].parentIndex, i);
        }
    }

    // Parcourt les meshes ; prend le premier primitive ayant POSITION + JOINTS_0 + WEIGHTS_0.
    // Pour Y Bot il n'y a qu'un seul mesh / primitive, mais on reste défensif.
    bool meshLoaded = false;
    for (cgltf_size mi = 0; mi < data->meshes_count && !meshLoaded; ++mi) {
        const cgltf_mesh* mesh = &data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh->primitives_count && !meshLoaded; ++pi) {
            const cgltf_primitive* prim = &mesh->primitives[pi];
            const cgltf_accessor *aPos = nullptr, *aNor = nullptr, *aUv = nullptr,
                                 *aJoints = nullptr, *aWeights = nullptr;
            for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai) {
                const cgltf_attribute& at = prim->attributes[ai];
                if (at.type == cgltf_attribute_type_position) aPos = at.data;
                else if (at.type == cgltf_attribute_type_normal) aNor = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) aUv = at.data;
                else if (at.type == cgltf_attribute_type_joints && at.index == 0) aJoints = at.data;
                else if (at.type == cgltf_attribute_type_weights && at.index == 0) aWeights = at.data;
            }
            if (!aPos || !aJoints || !aWeights) continue;

            const size_t nv = aPos->count;
            out.vertices.resize(nv);
            for (size_t v = 0; v < nv; ++v) {
                SkinnedVertex& sv = out.vertices[v];
                cgltf_accessor_read_float(aPos, v, sv.pos, 3);
                if (aNor) {
                    cgltf_accessor_read_float(aNor, v, sv.normal, 3);
                } else {
                    sv.normal[0] = 0; sv.normal[1] = 0; sv.normal[2] = 1;
                }
                if (aUv) {
                    cgltf_accessor_read_float(aUv, v, sv.uv, 2);
                } else {
                    sv.uv[0] = 0; sv.uv[1] = 0;
                }
                cgltf_uint joints[4] = {0, 0, 0, 0};
                cgltf_accessor_read_uint(aJoints, v, joints, 4);
                for (int k = 0; k < 4; ++k) sv.boneIndices[k] = static_cast<uint16_t>(joints[k]);
                cgltf_accessor_read_float(aWeights, v, sv.weights, 4);
            }
            if (prim->indices) {
                const size_t ni = prim->indices->count;
                out.indices.resize(ni);
                for (size_t i = 0; i < ni; ++i) {
                    out.indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(prim->indices, i));
                }
            } else {
                // Pas d'index buffer : on génère une séquence 0..nv-1.
                out.indices.resize(nv);
                for (size_t i = 0; i < nv; ++i) out.indices[i] = static_cast<uint32_t>(i);
            }
            meshLoaded = true;
        }
    }
    if (!meshLoaded) {
        LOG_WARN(Render, "[SkinnedMeshLoader] No skinned primitive in '{}'", path);
        cgltf_free(data);
        return std::nullopt;
    }

    // Charge les animations. Chaque clip a une track par bone (taille = joints_count) ;
    // les bones non animés gardent leurs vectors vides (AnimationSampler::SamplePose
    // retombe alors sur bindLocal / identité / scale 1).
    /// Renvoie l'index du bone correspondant au node animé, ou -1 si le node
    /// n'est pas dans le skin (cas d'une animation qui cible un node non-bone — on l'ignore).
    auto FindBoneIndexFromNode = [&](const cgltf_node* n) -> int {
        for (cgltf_size i = 0; i < skin->joints_count; ++i) {
            if (skin->joints[i] == n) return static_cast<int>(i);
        }
        return -1;
    };
    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation* anim = &data->animations[ai];
        AnimationClip clip;
        clip.name = anim->name ? anim->name : ("anim_" + std::to_string(ai));
        clip.tracks.resize(skin->joints_count);
        float maxTime = 0.0f;
        for (cgltf_size ci = 0; ci < anim->channels_count; ++ci) {
            const cgltf_animation_channel* ch = &anim->channels[ci];
            int boneIdx = FindBoneIndexFromNode(ch->target_node);
            if (boneIdx < 0) continue;
            BoneTracks& trk = clip.tracks[boneIdx];
            const cgltf_animation_sampler* s = ch->sampler;
            if (!s) continue;
            // input->max[0] = temps de la dernière keyframe = durée du sampler.
            if (s->input && s->input->has_max) {
                if (s->input->max[0] > maxTime) maxTime = s->input->max[0];
            }
            if (ch->target_path == cgltf_animation_path_type_translation) {
                trk.translation = LoadKeyframes<engine::math::Vec3, 3>(s,
                    [](const float* v) { return engine::math::Vec3{v[0], v[1], v[2]}; });
            } else if (ch->target_path == cgltf_animation_path_type_rotation) {
                trk.rotation = LoadKeyframes<engine::math::Quat, 4>(s,
                    [](const float* v) { return engine::math::Quat{v[0], v[1], v[2], v[3]}; });
            } else if (ch->target_path == cgltf_animation_path_type_scale) {
                trk.scale = LoadKeyframes<engine::math::Vec3, 3>(s,
                    [](const float* v) { return engine::math::Vec3{v[0], v[1], v[2]}; });
            }
        }
        clip.duration = maxTime;
        out.clips.push_back(std::move(clip));
    }

    cgltf_free(data);
    return out;
}

/// Chemin de production : enchaîne parse CPU + upload GPU. Si l'une des deux
/// étapes échoue, renvoie nullopt (et les ressources partielles d'Upload sont
/// nettoyées par Upload lui-même).
std::optional<SkinnedMesh> SkinnedMeshLoader::Load(VkDevice device, VkPhysicalDevice physicalDevice,
                                                    const std::string& path)
{
    auto cpu = LoadCpuOnlyForTests(path);
    if (!cpu) return std::nullopt;
    SkinnedMesh m;
    if (!m.Upload(device, physicalDevice, *cpu)) return std::nullopt;
    return m;
}

/// Charge un .glb pour en extraire uniquement les clips d'animation, retargetés
/// par nom sur `targetSkeleton`. Permet de fusionner plusieurs fichiers Mixamo
/// (un par animation) sur un seul mesh joué — robuste à un réordonnancement
/// des joints entre exports tant que les noms restent stables.
///
/// \param path           Chemin du .glb source (peut contenir un skin différent).
/// \param targetSkeleton Squelette cible (mesh joué). Les tracks retournées sont
///                       indexées sur ses bones (size == targetSkeleton.bones.size()).
/// \return Vecteur de clips retargetés ; vide si parse échoue.
///         Les bones source non trouvés dans la cible sont silencieusement droppés.
std::vector<AnimationClip> SkinnedMeshLoader::LoadClipsRetargeted(const std::string& path,
                                                                    const Skeleton& targetSkeleton)
{
    auto loaded = LoadCpuOnlyForTests(path);
    if (!loaded) {
        return {};
    }

    const Skeleton& sourceSkel = loaded->skeleton;

    // Construit la table de mapping: index_bone_source -> index_bone_cible (par nom).
    // -1 signifie "bone source absent de la cible" -> la track sera droppée.
    std::vector<int> sourceToTarget(sourceSkel.bones.size(), -1);
    for (size_t s = 0; s < sourceSkel.bones.size(); ++s) {
        sourceToTarget[s] = targetSkeleton.FindBoneIndex(sourceSkel.bones[s].name);
    }

    std::vector<AnimationClip> retargeted;
    retargeted.reserve(loaded->clips.size());
    for (const auto& srcClip : loaded->clips) {
        AnimationClip dst;
        dst.name = srcClip.name;
        dst.duration = srcClip.duration;
        // Important : taille sur la CIBLE, pas sur la source — l'AnimationSampler
        // indexe tracks[] par bone index du squelette cible.
        dst.tracks.resize(targetSkeleton.bones.size());
        for (size_t s = 0; s < srcClip.tracks.size(); ++s) {
            const int t = sourceToTarget[s];
            if (t < 0) continue;  // bone source absent de la cible -- drop track
            dst.tracks[t] = srcClip.tracks[s];
        }
        retargeted.push_back(std::move(dst));
    }
    return retargeted;
}

/// Charge les clips d'un .glb sans skin (export Mixamo "without skin"). Parse
/// directement les channels d'animation et retarget par target_node->name sur
/// les bones du squelette cible. Bones absents = skip silencieux.
///
/// \param path           Chemin du .glb source (animation-only, typiquement < 500 KB).
/// \param targetSkeleton Squelette cible (mesh joué).
/// \return Vecteur de clips ; vide si parse échoue ou si aucune animation utile
///         (filtre `duration > 0` pour éliminer les clips "Take 001" vides de Mixamo).
/// Effet de bord : aucun (CPU only — pas d'allocation Vulkan).
std::vector<AnimationClip> SkinnedMeshLoader::LoadClipsAnimOnly(const std::string& path,
                                                                  const Skeleton& targetSkeleton)
{
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
        LOG_WARN(Render, "[SkinnedMeshLoader] LoadClipsAnimOnly parse failed for '{}'", path);
        if (data) cgltf_free(data);
        return {};
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        LOG_WARN(Render, "[SkinnedMeshLoader] LoadClipsAnimOnly buffer load failed for '{}'", path);
        cgltf_free(data);
        return {};
    }

    // Build a lookup : node name -> target bone index.
    // Unlike LoadClipsRetargeted (which iterates skin->joints), here we have no skin —
    // we use any node referenced as a channel target.
    auto FindBoneByName = [&](const char* name) -> int {
        if (!name) return -1;
        const std::string n = name;
        for (size_t i = 0; i < targetSkeleton.bones.size(); ++i) {
            if (targetSkeleton.bones[i].name == n) return static_cast<int>(i);
        }
        return -1;
    };

    std::vector<AnimationClip> out;
    out.reserve(data->animations_count);
    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation* anim = &data->animations[ai];
        AnimationClip clip;
        clip.name = anim->name ? anim->name : ("anim_" + std::to_string(ai));
        clip.tracks.resize(targetSkeleton.bones.size());
        float maxTime = 0.0f;

        for (cgltf_size ci = 0; ci < anim->channels_count; ++ci) {
            const cgltf_animation_channel* ch = &anim->channels[ci];
            if (!ch->target_node) continue;

            const int boneIdx = FindBoneByName(ch->target_node->name);
            if (boneIdx < 0) continue;  // node absent from target skeleton -> skip

            BoneTracks& trk = clip.tracks[boneIdx];
            const cgltf_animation_sampler* s = ch->sampler;
            if (!s) continue;
            if (s->input && s->input->has_max && s->input->max[0] > maxTime) {
                maxTime = s->input->max[0];
            }
            if (ch->target_path == cgltf_animation_path_type_translation) {
                trk.translation = LoadKeyframes<engine::math::Vec3, 3>(s,
                    [](const float* v){ return engine::math::Vec3{v[0], v[1], v[2]}; });
            } else if (ch->target_path == cgltf_animation_path_type_rotation) {
                trk.rotation = LoadKeyframes<engine::math::Quat, 4>(s,
                    [](const float* v){ return engine::math::Quat{v[0], v[1], v[2], v[3]}; });
            } else if (ch->target_path == cgltf_animation_path_type_scale) {
                trk.scale = LoadKeyframes<engine::math::Vec3, 3>(s,
                    [](const float* v){ return engine::math::Vec3{v[0], v[1], v[2]}; });
            }
        }
        clip.duration = maxTime;
        if (clip.duration > 0.0f) {
            out.push_back(std::move(clip));
        }
    }

    cgltf_free(data);
    return out;
}

}  // namespace engine::render::skinned
