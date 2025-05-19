[[toc]]

## dump hash

- [ ] 移除 createRootSignatureFromSubobjectLib API
  - create root signature from shader
    - workgraph 的 global root signature 和正常的 shader 的 root signature 不一樣的，可以直接呼叫 CreateRootSignature 創建嗎？


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