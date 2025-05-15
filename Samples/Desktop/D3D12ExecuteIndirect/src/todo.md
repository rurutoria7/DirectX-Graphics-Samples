[[toc]]

## bug: legacy 閃爍

- [ ] 比較兩邊的實現

| 著色器資源 | 類型 | Register | C++ 對應的 Buffer | 用途 |
|------------|------|----------|-------------------|------|
| inInstances | StructuredBuffer<Instance> | t0 | in_inst_buffer | 輸入的實例資料，包含世界矩陣和材質索引 |
| outInstances | RWStructuredBuffer<my_uint> | u0 | m_is_inst_alive_buffer | 輸出實例是否存活的標記 |
| outCommands | RWStructuredBuffer<IndirectCommand> | u1 | out_command_buffer | 輸出間接繪製命令 |
| vp (來自 CullInstancePass_common.hlsl) | float4x4 | b0 | m_cb.vp | 視圖投影矩陣 |


1. **Clear Buffer Pass** (`ClearBufferCS.hlsl`):
| 著色器資源 | 類型 | Register | C++ 對應的 Buffer | 用途 |
|------------|------|----------|-------------------|------|
| `is_inst_alive_buffer` | `RWStructuredBuffer<my_uint>` | `u0` | `m_is_inst_alive_buffer` | 實例存活標記緩衝區 |
| `scanned_group_sum_buffer` | `RWStructuredBuffer<my_uint>` | `u1` | `m_scanned_group_sum_buffer` | 掃描後的群組總和緩衝區 |
| `inst_newpos_buffer` | `RWStructuredBuffer<my_uint>` | `u2` | `m_inst_newpos_buffer` | 實例新位置緩衝區 |
| `group_sum_buffer` | `RWStructuredBuffer<my_uint>` | `u3` | `m_group_sum_buffer` | 群組總和緩衝區 |
| `command_buffer` | `RWStructuredBuffer<IndirectCommand>` | `u4` | `out_command_buffer` | 間接繪製命令緩衝區 |

2. **Scan Prefix Pass** (`ScanInstancesCS.hlsl`):
| 著色器資源 | 類型 | Register | C++ 對應的 Buffer | 用途 |
|------------|------|----------|-------------------|------|
| `instancePredicatesIn` | `StructuredBuffer<my_uint>` | `t0` | `m_is_inst_alive_buffer` | 輸入實例謂詞 |
| `groupSumArray` | `RWStructuredBuffer<my_uint>` | `u0` | `m_group_sum_buffer` | 群組總和陣列 |
| `scannedInstancePredicates` | `RWStructuredBuffer<my_uint>` | `u1` | `m_inst_newpos_buffer` | 掃描後的實例謂詞 |

3. **Scan Group Pass** (`ScanGroupsCS.hlsl`):
| 著色器資源 | 類型 | Register | C++ 對應的 Buffer | 用途 |
|------------|------|----------|-------------------|------|
| `groupSumArrayIn` | `StructuredBuffer<my_uint>` | `t0` | `m_group_sum_buffer` | 輸入群組總和陣列 |
| `groupSumArrayOut` | `RWStructuredBuffer<my_uint>` | `u0` | `m_scanned_group_sum_buffer` | 輸出掃描後的群組總和 |

4. **Copy Instance Pass** (`CopyInstancesCS.hlsl`):
| 著色器資源 | 類型 | Register | C++ 對應的 Buffer | 用途 |
|------------|------|----------|-------------------|------|
| `instanceDataIn` | `StructuredBuffer<Instance>` | `t0` | `in_inst_buffer` | 輸入實例資料 |
| `instancePredicatesIn` | `StructuredBuffer<my_uint>` | `t1` | `m_is_inst_alive_buffer` | 實例謂詞 |
| `groupSumArray` | `StructuredBuffer<my_uint>` | `t2` | `m_scanned_group_sum_buffer` | 群組總和陣列 |
| `scannedInstancePredicates` | `StructuredBuffer<my_uint>` | `t3` | `m_inst_newpos_buffer` | 掃描後的實例謂詞 |
| `instanceDataOut` | `RWStructuredBuffer<Instance>` | `u0` | `out_proccessed_inst_buffer` | 輸出處理後的實例資料 |

5. **Scan Command Pass** (`ScanCommandsCS.hlsl`):
| 著色器資源 | 類型 | Register | C++ 對應的 Buffer | 用途 |
|------------|------|----------|-------------------|------|
| `inoutCommands` | `RWStructuredBuffer<IndirectCommand>` | `u0` | `out_command_buffer` | 輸入輸出的間接繪製命令 |

所有著色器共用的常數緩衝區：
| 著色器資源 | 類型 | Register | C++ 對應的 Buffer | 用途 |
|------------|------|----------|-------------------|------|
| `vp` | `float4x4` | `b0` | `m_cb.vp` | 視圖投影矩陣 |
| `noof_instances` | `uint` | `b0` | `m_cb.noof_instances` | 實例數量 |
| `noof_instances_pow_of_2` | `uint` | `b0` | `m_cb.noof_instances_pow_of_2` | 實例數量的 2 的冪次方 |
| `noof_drawcalls` | `uint` | `b0` | `m_cb.noof_drawcalls` | 繪製呼叫數量 |


## migrate to work graph

### rules

- 如果要啟用 work graph，要在 defines.h define GR_WORKGRAPH
- dispatch 方式統一採用 broadcasting
- 統一定義節點屬性 NodeMaxDispatchGrid 為 defines.h 裡面的值
- dispatch grid 參考 defines.h --> OcclusionPassCB
    - 記得使用 ROOT_SIG_B1 宣告 Root Signature
    - Root Signature 都是宣告在 hlsl 裡面
- 整個 work graph 的 dispatch 尺寸，的方式是在 work graph 裡面的 entry point 來決定的，cpp 端只要 dispatchGraph(1,1,1)


### 如何在代碼層面移植到 work graph

1. **資源綁定方式的差異**：
```hlsl
// KillInstancesCS.hlsl - 使用 RootSignature 屬性
[RootSignature(
    ROOT_SIG_B1
    "SRV(t0), "
    "UAV(u0), "
    "UAV(u1)")]

// Dummy_wg.hlsl - 使用 GlobalRootSignature 結構
GlobalRootSignature globalRS = { 
    ROOT_SIG_B1 "SRV(t0), UAV(u0), UAV(u1)"
};
```

2. **執行模型差異**：
```hlsl
// KillInstancesCS.hlsl - 傳統 Compute Shader
[numthreads(NOOF_THREADS, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)

// Dummy_wg.hlsl - Work Graph 節點
[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(MAX_NOOF_INSTANCES/NOOF_THREADS_KILL_INSTANCES,1,1)]
[NumThreads(NOOF_THREADS, 1, 1)]
void secondNode(
    RWDispatchNodeInputRecord<secondNodeInput> inputData,
    uint threadIndex : SV_GroupIndex,
    uint dispatchThreadID : SV_DispatchThreadID)
```

3. **新增的數據結構**：
```hlsl
// Dummy_wg.hlsl - 新增的節點間通信結構
struct entryRecord
{
    uint pad;  // ignore
};

struct secondNodeInput
{
    uint gridSize : SV_DispatchGrid;
};

struct thirdNodeInput
{
    uint entryRecordIndex;
};
```

4. **新增的調度控制節點**：
```hlsl
// Dummy_wg.hlsl - 新增的 firstNode 用於控制調度
[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeDispatchGrid(1,1,1)]
[NumThreads(1, 1, 1)]
void firstNode(
    DispatchNodeInputRecord< entryRecord> inputData,
    [MaxRecords(1)] NodeOutput<secondNodeInput> secondNode,
    uint threadIndex : SV_GroupIndex,
    uint dispatchThreadID : SV_DispatchThreadID)
{
    GroupNodeOutputRecords<secondNodeInput> outRecs = secondNode.GetGroupNodeOutputRecords(1);
    outRecs[0].gridSize = noof_instances / NOOF_THREADS + 1;
    outRecs.OutputComplete();
}
```

5. **同步機制差異**：
```hlsl
// KillInstancesCS.hlsl - 使用 DeviceMemoryBarrierWithGroupSync
DeviceMemoryBarrierWithGroupSync();

// Dummy_wg.hlsl - 使用 Work Graph 的內建同步機制
// 通過節點間的數據流來實現同步
```

6. **參數傳遞差異**：
```hlsl
// KillInstancesCS.hlsl - 直接使用 SV_DispatchThreadID 等系統值
uint idx = DTid.x;

// Dummy_wg.hlsl - 通過節點輸入記錄傳遞參數
uint idx = dispatchThreadID.x;
```

主要的代碼差異在於：
1. Work Graph 版本需要更多的結構定義來支持節點間的通信
2. 執行模型從單一的 Compute Shader 變成了多節點的 Work Graph
3. 同步機制從顯式的屏障變成了隱式的節點間數據流
4. 參數傳遞方式從系統值變成了節點輸入記錄
5. 資源綁定方式從 RootSignature 屬性變成了 GlobalRootSignature 結構

這些差異反映了 Work Graph 的設計理念：更靈活的執行模型和更清晰的數據流。



## future todo

- 效能優化
    - [ ] 優化 material 數量, now numMaterial == numMeshes
    - [ ] 優化紋理數量，現在 numTexture == numMeshes * 3

## pipeline chart

```mermaid
flowchart TD
    RenderPass[Render Pass]

    subgraph Culling Pipeline
        KillPass[Kill Instance Pass]
        PrefixScanPass[Prefix Scan Pass]
        GroupScanPass[Group Scan Pass]
        CopyPass[Copy Instances Pass]
        CommandScanPass[Scan Command Buffer Pass]

        KillPass -->|Alive Flags Buffer| PrefixScanPass
        KillPass -->|Command Buffer| CommandScanPass
        PrefixScanPass -->|Group Sum Buffer| GroupScanPass
        GroupScanPass -->|Group Sum Buffer| CopyPass
        PrefixScanPass -->|Instance New Idx Buffer| CopyPass
    end

    CopyPass -->|Instance Buffer| RenderPass
    CommandScanPass -->|Command Buffer| RenderPass

```

```mermaid
flowchart LR
    RenderPass[Render Pass]

    subgraph Culling Pipeline
        KillPass[Kill Instance Pass]
        PrefixScanPass[Prefix Scan Pass]
        GroupScanPass[Group Scan Pass]
        CopyPass[Copy Instances Pass]
        CommandScanPass[Scan Command Buffer Pass]

        KillPass --> PrefixScanPass
        subgraph Compaction
        PrefixScanPass --> GroupScanPass
        GroupScanPass --> CopyPass
        end
        

        CopyPass --> CommandScanPass
    end
    CommandScanPass --> RenderPass
```