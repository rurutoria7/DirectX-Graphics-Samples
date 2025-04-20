## multi-mesh instance culling

- [ ] BUG: 不知道 command buffer 有沒有算對
    - 打開 nsight 看一下
    - offset 是多少？
    - render 到第二個 mesh 時： command offset 需要是對的，數量應該是對的


## next week

- tier 0
    - [x] single-mesh instance 剔除
    - [x] refactor code
- tier 1
    - [ ] multi-mesh 的 instance 剔除
    - [ ] 用 Nsight 來分析 naive compute shader 的效能
- tier 2
    - [ ] 有那些 workgraph 可以優化的空間？e.g. 根據 intermediate 的結果來節省掉不必要的 dispatch
    - [ ] 當前 pipeline 的 workgraph 版本移植
- tier 3
    - [ ] thread group size 的設置原理
    - [ ] 封裝 resource
    - [ ] belloch prefix sum 正確性説明


## todo

- 規格
    - [ ] drawcall compaction
- 效能優化
    - [ ] 優化 material 數量, now numMaterial == numMeshes
    - [ ] 優化紋理數量，現在 numTexture == numMeshes * 3
