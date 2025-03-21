/*
	def cs_fake_remove:
		get rw_buffer <Instance>: out_inst_buffer
		get buffer <Instance>: in_inst_buffer
		get cbuffer:
			num_instance
			mvp
			
		set numthreads_per_group(tg_sz, 1, 1)
		
		def main(): 
			thread_id = groupId.x * tg_sz + groupdIndex
			
			if (thread_id < num_instance):
				inst_pos = in_inst_buffer[thread_id] * mvp
				if (is_in_frustum(inst_pos))
					out_inst_buffer[thread_id] =
						in_inst_buffer[thread_id]
				else
					out_inst_buffer[thread_id] = 
						in_inst_buffer[thread_id]
					out_inst_buffer[thread_id].world = （100000， 100000）
*/

// CullInst.hlsl
// 此 compute shader 依據每個 instance 的 world 座標與傳入的 VP 矩陣進行裁剪
// 若 instance 經過轉換後不在裁剪空間內，則將其 world 座標改為 (100000, 100000)

// CullInst.hlsl
// 此 compute shader 使用 64 個執行緒/群組
// 根常數 (b0) 傳入：
//    int numInstance;      // instance 總數
//    float4x4 vp;          // view-projection 矩陣（已轉置）
//
// t0: StructuredBuffer<Instance> inInstances (原始 instance 資料)
// u0: RWStructuredBuffer<Instance> outInstances (輸出經過篩檢的 instance)

struct Instance
{
    float4x4 world;
    int4 materialIndex;
};

StructuredBuffer<Instance> inInstances : register(t0);
RWStructuredBuffer<Instance> outInstances : register(u0);

// 根常數結構，與 app 端設置一致：
// 先傳入 instance 數量，再傳入 4x4 vp 矩陣
cbuffer Constants : register(b0)
{
    int numInstance;
    float4x4 vp;
};

// 簡單的 clip 空間判斷函式，根據 D3D 的 clip space (z: [0, w])
bool isInFrustum(float4 clipPos)
{
    bool inside = (clipPos.x >= -clipPos.w) && (clipPos.x <= clipPos.w) &&
                  (clipPos.y >= -clipPos.w) && (clipPos.y <= clipPos.w) &&
                  (clipPos.z >= 0.0f) && (clipPos.z <= clipPos.w);
    return inside;
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    if (idx >= (uint) numInstance)
        return;

    // 讀取原始 instance 資料
    Instance inst = inInstances[idx];

    // 假設 world 矩陣的第 4 列為平移向量
    float3 pos = inst.world[3].xyz;
    float4 clipPos = mul(float4(pos, 1.0f), vp);

    // 判斷該 instance 是否位於可見區域
    //if (isInFrustum(clipPos))
    if (clipPos.x < 0)
    {
        outInstances[idx] = inst;
    }
    else
    {
        // 若不在可見區，將 instance 的平移設定到遠處以“剔除”
        inst.world[3] = float4(100000.0f, 100000.0f, 100000.0f, 1.0f);
        outInstances[idx] = inst;
    }
}
