#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <vector>
#include <string>
#include <iostream>
#include <DirectXMath.h>

#ifndef MYMESH_H
#define MYMESH_H

#define MODEL_SCALE (10.0f)
#define DEVELOP_INSTANCE

namespace OWO
{
    struct Instance {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMINT4 materialIndex;
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
#endif


#ifdef IMPLEMENT_FBXLOADER
namespace OWO
{
    bool FBXLoader::LoadFBX( const std::string& filepath )
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

        /* TOODOO: Read instance data from file (low priority)
        * possible API: FBXLoader::AttachInstanceData( int meshIndex, const std::string& filepath )
        * called after LoadFBX
        * 
        */
#ifdef DEVELOP_INSTANCE
        auto mock_instance_data = [&]( int mesh_id ) -> std::vector<Instance>
            {
                if ( mesh_id == 0 )                // 5 instance with offset
                {
                    std::vector<Instance> res;
                    float spacing = 20;
                    for ( int i = 0; i < 50; i++ )
                    {
                        Instance inst;
                        auto world = XMMatrixTranslation( spacing * (i-25), 0, 0 );
                        world = XMMatrixMultiply( world, XMMatrixRotationX( XMConvertToRadians( 0.0f ) ) );
                        XMStoreFloat4x4( &inst.world, XMMatrixTranspose( world ) );
                        inst.materialIndex = XMINT4( i, 0, 0, 0 );
                        res.push_back( inst );
                    }
                    return res;
                }

                // 1 instance no offset, default data
                std::vector<Instance> res;
                XMMATRIX idm = XMMatrixIdentity();
                Instance inst;
                XMStoreFloat4x4( &inst.world, XMMatrixTranspose( idm ) );
                inst.materialIndex = XMINT4( 0, 0, 0, 0 );
                res.push_back( inst );

                return res;
            };

        for ( int i = 0; i < meshes.size(); i++ )
        {
            meshes[i].instances = mock_instance_data( i );
        }
#endif

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

    void FBXLoader::ProcessNode( aiNode* node, const aiScene* scene )
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

    Mesh FBXLoader::ProcessMesh( aiMesh* mesh, const aiScene* scene )
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

    Material FBXLoader::LoadMaterial( aiMaterial* mat )
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

    std::vector<Texture> FBXLoader::LoadTextures( aiMaterial* mat, aiTextureType type, const std::string& typeName )
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
#endif