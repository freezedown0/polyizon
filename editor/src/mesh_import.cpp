#include "mesh_import.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/glm.hpp>

#include <stdexcept>
#include <vector>

std::shared_ptr<polyizon::Mesh> LoadMesh(polyizon::VulkanContext& context, const std::filesystem::path& path) {
    Assimp::Importer importer;
    // Triangulate: this project's pipelines only ever draw triangle lists.
    // GenNormals: only used as a fallback for files that omit normals
    // entirely (this phase's own plane.obj/cube.obj always author them
    // explicitly - see core/models/). JoinIdenticalVertices: standard
    // Assimp post-process to dedupe vertices post-triangulation: it merges
    // by full vertex identity (position+normal+etc), so it never merges
    // across a hard edge where per-face normals genuinely differ (see
    // cube.obj's 24-vertex, one-normal-per-face authoring).
    const aiScene* scene = importer.ReadFile(path.string(),
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_JoinIdenticalVertices);

    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr) {
        throw std::runtime_error(
            "Failed to import mesh '" + path.string() + "': " + importer.GetErrorString());
    }
    if (scene->mNumMeshes == 0) {
        throw std::runtime_error("Mesh file '" + path.string() + "' contains no meshes");
    }

    // Only the first mesh: this phase's sample scene is one mesh per file
    // (plane.obj/cube.obj each contain exactly one) - no multi-mesh/scene-
    // graph import yet (that's tied to Phase 16+'s scene serialization work,
    // not this phase's scope).
    const aiMesh* mesh = scene->mMeshes[0];

    std::vector<polyizon::Vertex3D> vertices;
    vertices.reserve(mesh->mNumVertices);
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        polyizon::Vertex3D vertex{};
        vertex.position = glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z);
        // mesh->HasNormals() is always true here in practice (aiProcess_GenNormals
        // guarantees it), guarded anyway rather than assumed.
        if (mesh->HasNormals()) {
            vertex.normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
        } else {
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        vertices.push_back(vertex);
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(mesh->mNumFaces) * 3);
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        // aiProcess_Triangulate guarantees exactly 3 indices per face.
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            indices.push_back(face.mIndices[j]);
        }
    }

    return std::make_shared<polyizon::Mesh>(context, vertices, indices);
}
