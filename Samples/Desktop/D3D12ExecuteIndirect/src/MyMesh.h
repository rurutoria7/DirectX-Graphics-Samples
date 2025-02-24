#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <vector>
#include <string>
#include <iostream>

#ifndef MYMESH_H
#define MYMESH_H

namespace OWO
{
    struct Instance {
        //XMMATRIX world;
        //UINT materialIndex;
    };
    struct Vertex {
        float position[3];
        float normal[3];
        float uv[2];
    };

    struct Texture {
        std::string type;
        std::string path;
    };

    struct Material {
        std::vector<Texture> textures;
        float diffuseColor[3];

        /*
        * return -1 if not found
        */
        int GetTextureID(std::string type, const std::vector<Texture> &allTextures) {
            for (auto& tex : textures) {
                for (int i = 0; i < allTextures.size(); i++) {
                    if (tex.type == type && tex.path == allTextures[i].path) {
                        return i;
                    }
                }
            }
            return -1;
        }
    };

    struct Mesh {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        Material material;
    };

    class FBXLoader {
    public:
        FBXLoader() = default;
        bool LoadFBX(const std::string& filepath);
        std::vector<Mesh> meshes;

        std::vector<Texture> allTextures;
        std::vector<Material> allMaterials;
        std::vector<Vertex> allVertices;
        std::vector<unsigned int> allIndices;

        const unsigned int NumMeshes() const {
            return meshes.size();
        }
        const unsigned int GetTextureOffset(int meshIndex) const {
            unsigned int offset = 0;
            for (int i = 0; i < meshIndex; i++) {
                offset += meshes[i].material.textures.size();
            }
            return offset;
        }
        const unsigned int GetIndexOffset(int meshIndex) const {
            unsigned int offset = 0;
            for (int i = 0; i < meshIndex; i++) {
                offset += meshes[i].indices.size();
            }
            return offset;
        }
        const unsigned int GetVertexOffset(int meshIndex) const {
            unsigned int offset = 0;
            for (int i = 0; i < meshIndex; i++) {
                offset += meshes[i].vertices.size();
            }
            return offset;
        }
        const std::vector<Mesh>& GetMeshes() const {
            return meshes;
        }
        const std::vector<Vertex>& GetVertices() const {
            return allVertices;
        }
        const std::vector<unsigned int>& GetIndices() const {
            return allIndices;
        }
        const std::vector<Texture>& GetTextures() const {
            return allTextures;
        }

    private:
        void ProcessNode(aiNode* node, const aiScene* scene);
        Mesh ProcessMesh(aiMesh* mesh, const aiScene* scene);
        Material LoadMaterial(aiMaterial* mat);
        std::vector<Texture> LoadTextures(aiMaterial* mat, aiTextureType type, const std::string& typeName);
    };
}
#endif


#ifdef IMPLEMENT_FBXLOADER
namespace OWO
{
    bool FBXLoader::LoadFBX(const std::string& filepath) {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(filepath, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            std::cerr << "ERROR: Assimp failed to load FBX: " << importer.GetErrorString() << std::endl;
            return false;
        }

        meshes.clear();
        ProcessNode(scene->mRootNode, scene);
        
        // Flatten
        for (auto& mesh : meshes) {
            allVertices.insert(allVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
            allTextures.insert(allTextures.end(), mesh.material.textures.begin(), mesh.material.textures.end());
            allMaterials.push_back(mesh.material);
            allIndices.insert(allIndices.end(), mesh.indices.begin(), mesh.indices.end());
        }

        return true;
    }

    void FBXLoader::ProcessNode(aiNode* node, const aiScene* scene) {
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(ProcessMesh(mesh, scene));
        }
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            ProcessNode(node->mChildren[i], scene);
        }
    }

    Mesh FBXLoader::ProcessMesh(aiMesh* mesh, const aiScene* scene) {
        Mesh myMesh;

        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex = {};
            vertex.position[0] = mesh->mVertices[i].x;
            vertex.position[1] = mesh->mVertices[i].y;
            vertex.position[2] = mesh->mVertices[i].z;

            if (mesh->HasNormals()) {
                vertex.normal[0] = mesh->mNormals[i].x;
                vertex.normal[1] = mesh->mNormals[i].y;
                vertex.normal[2] = mesh->mNormals[i].z;
            }

            if (mesh->mTextureCoords[0]) {
                vertex.uv[0] = mesh->mTextureCoords[0][i].x;
                vertex.uv[1] = mesh->mTextureCoords[0][i].y;
            }

            myMesh.vertices.push_back(vertex);
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                myMesh.indices.push_back(face.mIndices[j]);
            }
        }

        if (mesh->mMaterialIndex >= 0) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            myMesh.material = LoadMaterial(material);
        }

        return myMesh;
    }

    Material FBXLoader::LoadMaterial(aiMaterial* mat) {
        Material material;
        aiColor3D color(0.0f, 0.0f, 0.0f);

        if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
            material.diffuseColor[0] = color.r;
            material.diffuseColor[1] = color.g;
            material.diffuseColor[2] = color.b;
        }

        material.textures = LoadTextures(mat, aiTextureType_DIFFUSE, "diffuse");
        return material;
    }

    std::vector<Texture> FBXLoader::LoadTextures(aiMaterial* mat, aiTextureType type, const std::string& typeName) {
        std::vector<Texture> textures;
        for (unsigned int i = 0; i < mat->GetTextureCount(type); i++) {
            aiString path;
            if (mat->GetTexture(type, i, &path) == AI_SUCCESS) {
                Texture texture;
                texture.type = typeName;
                texture.path = path.C_Str();
                textures.push_back(texture);
            }
        }
        return textures;
    }
}
#endif