## multi-mesh instance culling

- bug description: 多個 drawcall，剔除的結果（command buffer）數量或是 position 有問題
    - 可能性一：在 kill instance 累计可见实例数量时，错误（command_buffer -> instance_count）
    - 可能性而：在前缀和算 instance offset 时错误
    - [x] profile 在一个 draw command 下，实例数量累计正确
        - 实例数量在哪一个栏位？第四个
    - [ ] profile 确保在多个 draw command 下，实例数量累计正确
        - 多放一个 mesh


## next week

- tier 0
    - [x] single-mesh instance 剔除
    - [x] refactor code
- tier 1
    - [ ] multi-mesh 的 instance 剔除
    - [ ] 用 Nsight 來分析 naive compute shader 的效能
- tier 2
    - [ ] thread group size 的設置原理
    - [ ] 有那些 workgraph 可以優化的空間？e.g. 根據 intermediate 的結果來節省掉不必要的 dispatch
    - [ ] 當前 pipeline 的 workgraph 版本移植
    - [ ] 优化原子加法 wave intrinsics 
- tier 3
    - [ ] 封裝 resource
    - [ ] belloch prefix sum 正確性説明


## todo

- 規格
    - [ ] drawcall compaction
- 效能優化
    - [ ] 優化 material 數量, now numMaterial == numMeshes
    - [ ] 優化紋理數量，現在 numTexture == numMeshes * 3


## 神的国

主，我的神
把一切挂虑卸下给你
喔
主 耶稣
耶和华救恩
耶稣
你的爱子
你所喜悦的
一个人位
这人位的一切，都是我们的救恩
万国必因你得福
