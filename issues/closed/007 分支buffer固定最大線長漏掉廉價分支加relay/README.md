# 分支 buffer 固定最大線長漏掉廉價分支加 relay

優先級：中（Score 可降低 95%）。檢查 commit `09d2cc2`。

## 反例

SRC=(3,3)，fanout=1、length=100；sinks=(20,10),(20,30)；chip=40x40，w_skew=1。

| 型別 | fanout | length | cost |
| --- | --- | --- | --- |
| T0 | 2 | 6 | 1 |
| T1 | 2 | 100 | 100 |

目前 actual-output001：一個 T1 分支 buffer=(20,20)，SRC 到 buffer 長 34，兩個 sink 距離均 10；T_max=T_min=44、cost/Score=100。

附件 witness-output001：同一分支點 (20,20)，用 T0 並經 (20,17)->(20,11)->S1 及 (20,23)->(20,29)->S2。分支 outgoing length=3+3=6；後續 edges 各為 6、1。共五個 T0，T_max=T_min=44、Score=5。座標嚴格內部且不同，全部 driving constraints 符合。

附件只證明更便宜合法解，未聲稱 Score=5 為全域下界。

## 原因與建議

Builder::branching_type 固定選 fanout>=2、length>=2 中 length 最大型別；cluster 用該型別的 length 分配兩個分支 routing budget。本例 limit=100，直接接兩個 sinks，不插 relay。finish 面對總 outgoing length=20，不能把 T1 降為 T0。

現有 relay spacing/mixed-chain policies 只改 relay_type，不能改分支根的 budget，因此即使便宜 relay chain 已支援，仍沒有建構此方案。單 hub 搜尋同樣要求 hub 直接驅動全部 sinks，也只會選 T1；source star 因 SRC fanout=1 不可用。

建議增加 branching type／budget 的成本候選，搭配既有單一與 mixed relay 政策比較完整 Score；或在局部最優化中把昂貴分支型別改成便宜型別並細分 outgoing edges。保留原最大 length 回退。限制政策組合與運算預算，避免 branch×relay×hub 重複建構超過 60 秒；按真實 weighted objective 選解，維持每個 parent 的「總」outgoing length。

## 重現與驗收

```sh
cmake -S . -B tmp/tester/branch-cost-build -DCMAKE_BUILD_TYPE=Release
cmake --build tmp/tester/branch-cost-build -j2
./tmp/tester/branch-cost-build/cbi 'issues/closed/007 分支buffer固定最大線長漏掉廉價分支加relay/input001.cbi' tmp/tester/branch-output001.cbi
python3 'issues/closed/007 分支buffer固定最大線長漏掉廉價分支加relay/verify_witness.py'
```

驗收：附件合法且 Score<=5；檢查長分支型別更便宜時仍可選長型別、cheap type fanout 不足、總 length 各分支分配與 relay 佔位回退。保留已關閉 issues 的 Score 基準，並提供新增組合對 runtime 的觀察。
