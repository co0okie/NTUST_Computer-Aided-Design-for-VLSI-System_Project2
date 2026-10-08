# relay 間距政策缺少中段混合型別優化

優先級：中（下一階段成本優化）。檢查 commit `98dafbf`。原「固定最大線長」issue 已驗收；本 issue 提供不同型別在同一 unary chain 中混用的具體機會。

## 反例

SRC=(2,2)，sink=(23,2)，chip=40x40，SRC fanout=1、length=1，w_skew=1。Library：T0 fanout=1、length=8、cost=5；T1 fanout=1、length=6、cost=4。

目前輸出：relay=(3,2),(11,2),(19,2)，型別 T0,T0,T1；後續段長 8,8,4，arrival=21、Score=14。

附件合法 witness：relay=(3,2),(11,2),(17,2)，型別 T0,T1,T1；後續段長 8,6,6，arrival=21、Score=13。兩份樹各有三個 buffer，Score 差異純粹來自 spacing/type 選擇。

Score=13 是此小案例下界：SRC 最多覆蓋 1，buffers 至少要覆蓋 20；兩個最大 length buffers 只能覆蓋 16，所以至少三個。三個 T1 成本 12 但只能覆蓋 18；至少一個 T0，最低成本為 5+4+4=13。更多 buffers 的成本至少 16。

## 原因及建議

每個 Builder 選一個固定 relay_policy：route 之後都用同一 capacity。finish 可以把短尾段降型別，但無法把原本第二段的 8 改成 6 並移動後面的 relay。純 T0 policy 只找到 8+8+4，純 T1 policy 需要四個 buffer，兩者都漏掉 8+6+6。

建議在 unary relay chain 進行成本與可覆蓋距離的動態規劃／有限 Pareto 搜尋，或對已建構的鏈作局部 spacing/type 重排。先處理無佔位的直線鏈，再擴展到 Manhattan 路徑與佔位回退；保留既有最大 length／單一政策候選作基準。避免對巨大 length 線性配置陣列，需遵守 60 秒及 8GB 限制。使用完整 validate_tree 與真實 Score 驗證，不能為降低 buffer cost 增加 skew 後忽略 objective。

## 重現與驗收

```sh
cmake -S . -B tmp/tester/mixed-chain-build -DCMAKE_BUILD_TYPE=Release
cmake --build tmp/tester/mixed-chain-build -j2
./tmp/tester/mixed-chain-build/cbi 'issues/closed/006 relay間距政策缺少中段混合型別優化/input001.cbi' tmp/tester/mixed-output001.cbi
python3 'issues/closed/006 relay間距政策缺少中段混合型別優化/verify_witness.py'
```

驗收：附件合法且 Score=13、arrival=21；補混合型別順序、短尾段、固定 pins 擋住理想間距的對照，維持先前 Score=6/4、hub=10/16、多 relay=19 等基準。未要求所有一般 clock trees 達全域最優。
