//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#define threadBlockSize 64

cbuffer RootConstants : register(b0)
{
    uint tri_count;
    float4x4 mvp;
};

struct VertexInput
{
    float3 position : POSITION; // ¦ì¸m (R32G32B32_FLOAT)
    float3 normal : NORMAL; // ªk½u (R32G32B32_FLOAT)
    float2 texcoord : TEXCOORD; // UV (R32G32_FLOAT)
};

StructuredBuffer<uint> input_index_buffer : register(t0);
RWStructuredBuffer<uint> output_index_buffer : register(u0);
StructuredBuffer<VertexInput> vert_buffer : register(t1);

[numthreads(threadBlockSize, 1, 1)]
void main(uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    uint tri_id = (groupId.x * threadBlockSize) + groupIndex;
    uint first_idx_id = tri_id * 3;
    
    if (tri_id < tri_count)
    {
        int out_of_frustum_count = 0;

        float4 positions[3];
        for (int i = 0; i < 3; i++)
        {
            int idx = input_index_buffer[first_idx_id + i];
            positions[i] = float4(vert_buffer[idx].position, 1.0f);
            positions[i] = mul(positions[i], mvp);

            positions[i] /= positions[i].w;

            if ((positions[i].x < -1.0f || positions[i].x > 1.0f) ||
                (positions[i].y < -1.0f || positions[i].y > 1.0f))
            {
                out_of_frustum_count++;
            }
        }

        bool culled_flag = true && (out_of_frustum_count == 3);

        for (int i = 0; i < 3; i++)
        {
            int x = first_idx_id + i;
            if (!culled_flag)
            {
                output_index_buffer[x] = input_index_buffer[x];
            }
            else
            {
                output_index_buffer[x] = input_index_buffer[first_idx_id];
            }
        }
    }
}

/* CullTri pseudo code

main:
    tri_id = (groupId.x * threadBlockSize) + groupIndex
    
    first_idx_id = triId * 3

    if tri_id < tri_count:
        for i = 0, 1, 2:
            x = first_idx_id + i
            float4 pos = float4( vert_buffer[ in_idx_buffer[ x ] ], 1.0f )
            pos = mul( pos, mvp )
            
            if ( pos.x < -1.0f || pos.x > 1.0f || pos.y < -1.0f || pos.y > 1.0f || pos.z < 0.0f || pos.z > 1.0f )
                discard = true
         for i = 0, 1, 2:
            x = first_idx_id + i
            if ( !discard )
                out_idx_buffer[ x ] = in_idx_buffer[ x ]
            else
                out_idx_buffer[ x ] = in_idx_buffer[ first_idx_id ]
*/
