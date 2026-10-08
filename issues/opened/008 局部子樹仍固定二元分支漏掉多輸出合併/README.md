# 局部子樹仍固定二元分支漏掉多輸出合併

優先級：中（Score 降低 50%）。檢查程式 commit `70c5769`；後續 `4cc8546` 無程式變更。

## 反例

SRC=(10,2)，fanout=1、length=8；sinks=(12,10),(10,12),(8,10),(9,9)；chip=20x20，w_skew=1；唯一 T0 fanout=3、length=100、cost=10。

目前 actual-output001 使用四顆 buffer，T_max=T_min=12，Score=40。

附件 witness-output001：B1=(10,10)，B2=(9,10)；SRC->B1，B1->{S1,S2,B2}，B2->{S3,S4}。SRC edge=8；B1 outgoing distances=2,2,1（total5、fanout3）；B2 outgoing distances=1,1（total2、fanout2）。所有 sink arrival=10，buffer cost=20，Score=20；座標內部且互異。

Score=20 同時為此例下界：SRC fanout=1，單一 buffer fanout=3 無法涵蓋四 leaves，因此至少兩顆 buffer；每顆 cost=10，skew 非負。附件正好達下界。

## 原因與建議

cluster 仍只做二元切分及每個根兩個 children，所以四 leaves 至少產生三個 branching buffers，另可能需要 source relay。build_hub 僅考慮單一 hub 直接驅動「全部」sinks，當四 sinks 超過 library fanout=3 時整個策略跳過；source star 因 SRC fanout=1 不可用。已關閉 issue 003 修正了全部 sinks 的單 hub，尚未涵蓋局部 multiway group 或 buffer->buffer 混合 fanout。

建議允許 cluster 的局部多輸出合併（children 可含 sink 與子樹 root），或對既有樹做 buffer bypass／子樹 flatten 後重新平衡位置與 arrival。逐 parent 檢查 fanout 與總 outgoing length；移除節點時更新 occupied、cost、可達性、level 與所有 sink arrival。以完整 weighted Score 接受改善，保留二元候選回退。局部候選需有明確運算上限，避免組合暴增。

## 重現與驗收

```sh
cmake -S . -B tmp/tester/local-multiway-build -DCMAKE_BUILD_TYPE=Release
cmake --build tmp/tester/local-multiway-build -j2
./tmp/tester/local-multiway-build/cbi 'issues/opened/008 局部子樹仍固定二元分支漏掉多輸出合併/input001.cbi' tmp/tester/local-multiway-output001.cbi
python3 'issues/opened/008 局部子樹仍固定二元分支漏掉多輸出合併/verify_witness.py'
```

驗收：附件合法且 Score=20；補 fanout 不足、每條 edge 合法但總 outgoing length 超限、局部合併改變 skew 的對照；維持既有 Score 基準，提供局部搜尋 runtime 的觀察。未要求任意 clock tree 全域最優。
