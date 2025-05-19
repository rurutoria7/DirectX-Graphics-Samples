#pragma once
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <DirectXMath.h>
#include <random>
#include <filesystem>
#include "render_pass/CullInstancePass.h"

#define MODEL_SCALE (1.0f)

namespace OWO
{
    struct Instance {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMINT4 materialIndex;  // (drawcall_id, 0, 0, 0)
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
        int GetTextureID( std::string type, const std::vector<Texture>& allTextures )
        {
            for ( auto& tex : textures )
            {
                for ( int i = 0; i < allTextures.size(); i++ )
                {
                    if ( tex.type == type && tex.path == allTextures[i].path )
                    {
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
        std::vector<Instance> instances;
        Material material;
    };

    class FBXLoader {
    public:
        FBXLoader() = default;
        bool LoadFBX( const std::string& filepath );
        std::vector<Mesh> meshes;

        std::vector<Texture> allTextures;
        std::vector<Material> allMaterials;
        std::vector<Vertex> allVertices;
        std::vector<Instance> allInstance;
        std::vector<unsigned int> allIndices;

        const unsigned int NumMeshes() const
        {
            return meshes.size();
        }
        const unsigned int GetTextureOffset( int meshIndex ) const
        {
            unsigned int offset = 0;
            for ( int i = 0; i < meshIndex; i++ )
            {
                offset += meshes[i].material.textures.size();
            }
            return offset;
        }
        const unsigned int GetIndexOffset( int meshIndex ) const
        {
            unsigned int offset = 0;
            for ( int i = 0; i < meshIndex; i++ )
            {
                offset += meshes[i].indices.size();
            }
            return offset;
        }
        const unsigned int GetVertexOffset( int meshIndex ) const
        {
            unsigned int offset = 0;
            for ( int i = 0; i < meshIndex; i++ )
            {
                offset += meshes[i].vertices.size();
            }
            return offset;
        }
        const unsigned int GetInstanceOffset( int meshIndex ) const
        {
            unsigned int offset = 0;
            for ( int i = 0; i < meshIndex; i++ )
            {
                offset += meshes[i].instances.size();
            }
            return offset;
        }

        const std::vector<Mesh>& GetMeshes() const
        {
            return meshes;
        }
        const std::vector<Vertex>& GetVertices() const
        {
            return allVertices;
        }
        const std::vector<Instance>& GetInstances() const
        {
            return allInstance;
        }
        const std::vector<unsigned int>& GetIndices() const
        {
            return allIndices;
        }
        const std::vector<Texture>& GetTextures() const
        {
            return allTextures;
        }

    private:
        void ProcessNode( aiNode* node, const aiScene* scene );
        Mesh ProcessMesh( aiMesh* mesh, const aiScene* scene );
        Material LoadMaterial( aiMaterial* mat );
        std::vector<Texture> LoadTextures( aiMaterial* mat, aiTextureType type, const std::string& typeName );
    };
}


namespace OWO
{
    inline bool FBXLoader::LoadFBX( const std::string& filepath )
    {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile( filepath, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals );

        if ( !scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode )
        {
            std::cerr << "ERROR: Assimp failed to load FBX: " << importer.GetErrorString() << std::endl;
            return false;
        }

        meshes.clear();
        ProcessNode( scene->mRootNode, scene );

#ifdef DEVELOP_INSTANCE
        auto mock_instance_data = [&](int mesh_id) -> std::vector<Instance>
        {
            const int kInstanceCount = 2000 * 100 / 4 / 4;
            const float kAreaHalfSize = 200.0f;
            const float kAreaHeight = 50.0f;
            std::vector<Instance> instances;
            instances.reserve(kInstanceCount);

            std::mt19937 rng(mesh_id);
            std::uniform_real_distribution<float> dist(-kAreaHalfSize, kAreaHalfSize);
            std::uniform_real_distribution<float> dist_height( 0, kAreaHeight );

            for (int i = 0; i < kInstanceCount; ++i)
            {
                float x = dist( rng );
                float z = dist( rng );
                float y = dist_height( rng );

                DirectX::XMMATRIX translation = DirectX::XMMatrixTranslation(x, y, z);

                DirectX::XMMATRIX worldMat = DirectX::XMMatrixTranspose(translation);

                Instance inst;
                DirectX::XMStoreFloat4x4(&inst.world, worldMat);
                inst.materialIndex = DirectX::XMINT4(mesh_id, 0, 0, 0);

                instances.push_back(inst);
            }

            return instances;
        };

        for ( int i = 0; i < meshes.size(); i++ )
        {
            meshes[i].instances = mock_instance_data( i );
        }
#endif

        

#ifdef DEV_LOAD_INSTANCE_BLOB
        auto generate_instance_data_blob = [&](const std::string& filepath) 
        {
            auto get_instance_count = [&](int mesh_id) -> int
            {
                return MAX_NOOF_INSTANCES / 4;
            };
            const float kAreaHalfSize = 200.0f;
            const float kAreaHeight = 50.0f;

            std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                std::cerr << "ERROR: Failed to create instance data file: " << filepath << std::endl;
                return;
            }

            for (int mesh_id = 0; mesh_id < meshes.size(); mesh_id++) {

                std::mt19937 rng(mesh_id);
                std::uniform_real_distribution<float> dist(-kAreaHalfSize, kAreaHalfSize);
                std::uniform_real_distribution<float> dist_height(0, kAreaHeight);
                std::uniform_real_distribution<float> rot_dist(0, DirectX::XM_2PI);

                int count = get_instance_count(mesh_id);

                file.write(reinterpret_cast<char*>(&count), sizeof(int));

                for (int i = 0; i < count; i++) {

#ifdef DEV_LOAD_INSTANCE_BLOB_RANDOM
                    float offset_x = dist(rng);
                    float offset_y = dist_height(rng);
                    float offset_z = dist(rng);
                    float rot_x = rot_dist(rng);
                    float rot_y = rot_dist(rng);
                    float rot_z = rot_dist(rng);
#else
                    float offset_x = i % 128 * 2;
                    float offset_y = mesh_id * 3 + (i / 128);
                    float offset_z = i / 128 * 2;
                    float rot_x = 0;
                    float rot_y = 0;
                    float rot_z = 0;
#endif

                    file.write(reinterpret_cast<char*>(&offset_x), sizeof(float));
                    file.write(reinterpret_cast<char*>(&offset_y), sizeof(float));
                    file.write(reinterpret_cast<char*>(&offset_z), sizeof(float));
                    file.write(reinterpret_cast<char*>(&rot_x), sizeof(float));
                    file.write(reinterpret_cast<char*>(&rot_y), sizeof(float));
                    file.write(reinterpret_cast<char*>(&rot_z), sizeof(float));
                }
            }

            int terminator = -1;
            file.write(reinterpret_cast<char*>(&terminator), sizeof(int));
            file.close();
        };
#endif        
        auto load_instance_data_blob = [this](const std::string& filepath)
        {
        /**
         * @brief Loads instance data from a binary file and populates the mesh instances.
         * 
         * The binary file is expected to contain instance data in the following format:
         * 
         * 1. A sequence of instance groups, where i-th group represents the i-th mesh in the scene. Each group consists of:
         *    - An signed integer (4 bytes), specifying the number of instances in the group (`noof_inst`).
         *    - A sequence of `noof_inst` instances, where each instance consists of:
         *      - float offset_x (4 bytes): The x-coordinate of the instance's translation offset.
         *      - float offset_y (4 bytes): The y-coordinate of the instance's translation offset.
         *      - float offset_z (4 bytes): The z-coordinate of the instance's translation offset.
         *      - float rot_x (4 bytes): The rotation angle around the x-axis in radians.
         *      - float rot_y (4 bytes): The rotation angle around the y-axis in radians.
         *      - float rot_z (4 bytes): The rotation angle around the z-axis in radians.
         * 
         * 2. The sequence ends when either:
         *    - A negative value is encountered for `noof_inst`, or
         *    - End of file (EOF) is reached after the last complete group
         * 
         * @param filepath The path to the binary file containing the instance data.
         * 
         * @note The function assumes that the binary file is well-formed and does not perform
         *       extensive validation of the file's contents. If the file is malformed, the behavior
         *       is undefined.
         * @note The `meshes` container is updated with the loaded instances, where each group of
         *       instances corresponds to a different mesh ID.
         */                        
            struct Inst {
                float offset_x;
                float offset_y;
                float offset_z;
                float rot_x;
                float rot_y;
                float rot_z;
            };

            std::ifstream file(filepath, std::ios::binary);
            if (!file.is_open())
            {
                OutputDebugStringW( L"ERROR: Failed to open instance data file \n" );
                std::exit(0);
            }

            file.seekg(0, std::ios::end);
            size_t fileSize = file.tellg();
            file.seekg(0, std::ios::beg);

            auto following_num = [&]()
            {
                if (file.eof() || !file.good()) return -1; // Check EOF or bad state
                int value;
                file.read(reinterpret_cast<char*>(&value), sizeof(int));
                return file.eof() ? -1 : value;
            };

            for (int mesh_id = 0; mesh_id < meshes.size(); mesh_id++ )
            {
                int noof_inst = following_num();
                if ( noof_inst < 0 ) break;
                std::vector<Inst> _inst;
                _inst.resize(noof_inst);
                file.read(reinterpret_cast<char*>(_inst.data()), noof_inst * sizeof(Inst));

                for (const auto& inst : _inst)
                {
                    DirectX::XMMATRIX translation = DirectX::XMMatrixTranslation(inst.offset_x, inst.offset_y, inst.offset_z);
                    DirectX::XMMATRIX rotationX = DirectX::XMMatrixRotationX(inst.rot_x);
                    DirectX::XMMATRIX rotationY = DirectX::XMMatrixRotationY(inst.rot_y);
                    DirectX::XMMATRIX rotationZ = DirectX::XMMatrixRotationZ(inst.rot_z);

                    DirectX::XMMATRIX worldMat = DirectX::XMMatrixTranspose(rotationX * rotationY * rotationZ * translation);

                    Instance instance;
                    DirectX::XMStoreFloat4x4(&instance.world, worldMat);
                    instance.materialIndex = DirectX::XMINT4(mesh_id, 0, 0, 0);

                    meshes[mesh_id].instances.push_back(instance);
                }
            }

            file.close();
        };

        std::string instanceDataPath = filepath + ".instances";
        generate_instance_data_blob(instanceDataPath);
        load_instance_data_blob(instanceDataPath);

        // Flatten
        for ( auto& mesh : meshes )
        {
            allVertices.insert( allVertices.end(), mesh.vertices.begin(), mesh.vertices.end() );
            allTextures.insert( allTextures.end(), mesh.material.textures.begin(), mesh.material.textures.end() );
            allInstance.insert( allInstance.end(), mesh.instances.begin(), mesh.instances.end() );
            allMaterials.push_back( mesh.material );
            allIndices.insert( allIndices.end(), mesh.indices.begin(), mesh.indices.end() );
        }

        return true;
    }

    inline void FBXLoader::ProcessNode( aiNode* node, const aiScene* scene )
    {
        for ( unsigned int i = 0; i < node->mNumMeshes; i++ )
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back( ProcessMesh( mesh, scene ) );
        }
        for ( unsigned int i = 0; i < node->mNumChildren; i++ )
        {
            ProcessNode( node->mChildren[i], scene );
        }
    }

    inline Mesh FBXLoader::ProcessMesh( aiMesh* mesh, const aiScene* scene )
    {
        Mesh myMesh;

        for ( unsigned int i = 0; i < mesh->mNumVertices; i++ )
        {
            Vertex vertex = {};
            vertex.position[0] = mesh->mVertices[i].x * MODEL_SCALE;
            vertex.position[1] = mesh->mVertices[i].y * MODEL_SCALE;
            vertex.position[2] = mesh->mVertices[i].z * MODEL_SCALE;

            if ( mesh->HasNormals() )
            {
                vertex.normal[0] = mesh->mNormals[i].x;
                vertex.normal[1] = mesh->mNormals[i].y;
                vertex.normal[2] = mesh->mNormals[i].z;
            }

            if ( mesh->mTextureCoords[0] )
            {
                vertex.uv[0] = mesh->mTextureCoords[0][i].x;
                vertex.uv[1] = mesh->mTextureCoords[0][i].y;
            }

            myMesh.vertices.push_back( vertex );
        }

        for ( unsigned int i = 0; i < mesh->mNumFaces; i++ )
        {
            aiFace face = mesh->mFaces[i];
            for ( unsigned int j = 0; j < face.mNumIndices; j++ )
            {
                myMesh.indices.push_back( face.mIndices[j] );
            }
        }

        if ( mesh->mMaterialIndex >= 0 )
        {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
            myMesh.material = LoadMaterial( material );
        }

        return myMesh;
    }

    inline Material FBXLoader::LoadMaterial( aiMaterial* mat )
    {
        Material material;
        aiColor3D color( 0.0f, 0.0f, 0.0f );

        if ( mat->Get( AI_MATKEY_COLOR_DIFFUSE, color ) == AI_SUCCESS )
        {
            material.diffuseColor[0] = color.r;
            material.diffuseColor[1] = color.g;
            material.diffuseColor[2] = color.b;
        }

        material.textures = LoadTextures( mat, aiTextureType_DIFFUSE, "diffuse" );
        return material;
    }

    inline std::vector<Texture> FBXLoader::LoadTextures( aiMaterial* mat, aiTextureType type, const std::string& typeName )
    {
        std::vector<Texture> textures;
        for ( unsigned int i = 0; i < mat->GetTextureCount( type ); i++ )
        {
            aiString path;
            if ( mat->GetTexture( type, i, &path ) == AI_SUCCESS )
            {
                Texture texture;
                texture.type = typeName;
                texture.path = path.C_Str();
                textures.push_back( texture );
            }
        }
        return textures;
    }
}
