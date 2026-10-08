# hub 選點漏算 SRC 接入 relay 成本

優先級：中（可重現 Score 優化缺口）。檢查 commit：`997687c`。

附件 input001：SRC=(6,1)、fanout=1、length=1；sinks=(3,1),(7,9),(7,1)；T0 fanout=3、length=30、cost=10；w_skew=1。

目前輸出 actual-output001：hub=(5,5)，接入 relay=(6,2)，兩個 buffer，T_max=T_min=11，Score=20。

附件 witness-output001：只用 B1=(6,2)，SRC edge 長度 1，hub outgoing distances=4,8,2，總和 14<=30，fanout=3。arrival=5,9,3，skew=6，cost=10，Score=16。所有元件嚴格內部且不同位置；合法樹已以獨立 helper 驗證。這是較好解的證據，未宣稱全域最優。

## 原因及建議

`Builder::build_hub()` 評選位置時只用 `type.cost + skew*w_skew`，SRC 距離僅當相同分數的 tie breaker。先挑「不含接入」Score=10 的遠端零 skew hub，再 route SRC 插入額外成本 10 的 relay，完整 Score 變為 20。鄰近位置 (6,2) 原本已在 candidates 中，但不含接入 Score=16 而被提前淘汰，完整樹比較沒有機會看到它。

建議把每個有希望的 hub 位置接好 SRC，重選 cheapest compatible types，使用完整合法樹的真實 Score 比較；或用可證明的下界先剪枝，不能只用 hub cost 淘汰。各位置評估應使用獨立 occupied/tree state，避免失敗 routing 留下元件影響後续候選。同樣可保留第二候選回退，避免最佳未接入 hub 無法合法連 SRC 時整個 hub strategy 失敗。

## 重現與驗收

```sh
cmake -S . -B tmp/tester/hub-review-build -DCMAKE_BUILD_TYPE=Release
cmake --build tmp/tester/hub-review-build -j2
./tmp/tester/hub-review-build/cbi 'issues/closed/004 hub選點漏算SRC接入relay成本/input001.cbi' tmp/tester/output001.cbi
python3 'issues/closed/004 hub選點漏算SRC接入relay成本/verify_witness.py'
```

驗收：此例合法輸出且 Score<=16；原四 sink 等距 issue 的 Score=10 不回退；增加需多個接入 relay 的對照及相容型別重選驗證，確保比較完整 buffer cost，並保留 driving limits。
