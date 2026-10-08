# 局部建構候選無回退，漏掉小型合法樹

優先級：高（已知合法輸入無输出）。檢查來源 commit `51e094e`。

## 反例

chip=5x5；SRC=(2,1)，fanout=2、length=4；四 sinks S1=(2,4)、S2=(3,4)、S3=(1,2)、S4=(1,3)。唯一型別T0：fanout=2、length=5、cost=1；w_skew=100。

目前執行回傳失敗，stderr：`cbi: no legal tree constructed: library cannot drive all sinks from one hub`。所有sinks/library/limits均合法，並非只有不可解的fanout或boundary情形。

witness-output001（SRC分兩個group）：B1=(2,2)接S1/S2，B2=(3,3)接S3/S4。SRC outgoing length=1+3=4<=4；B1=2+3=5<=5；B2=3+2=5<=5。fanout全部2；所有buffer嚴格位於內部且座標唯一。T_max6/T_min3，cost2、Score302。

witness-output002（單一SRC根分支的binary tree）：B1=(2,2)接B2=(2,3)/B3=(3,3)，B2接S3/S4、B3接S1/S2。SRC length1<=4；B1=1+2=3<=5；B2=2+1=3<=5；B3=2+1=3<=5。T_max5/T_min3，cost3、Score203。這說明既有binary拓樸本身亦能有合法擺法，不必依賴超出library fanout的hub。

兩附件完整檢查pins、unique/interior、fanout/parent總length、連通、IDs/level與arrival均通過；此issue要求能輸出合法解，不主張附件Score最優。

## 建構缺口與建議

binary/parity建構在每個 `cluster` 只採用單一 `merge_position`；下游子樹的local選點會影響上游可用位置、route與source接入，但失敗後沒有回退替代merge點。`build_star`又要求SRC能逐一驅動全部sinks（本例4>fanout2），`build_hub`要求單buffer能驅動全部sinks（本例4>fanout2）。目前完整候選都失敗後，後處理bypass/relocation/交換/改接無合法初始樹可改善。

建議為小型建構提供有界回退：保留少數不同merge點/子樹候選、失敗時重試上游可接入的組合；也可加入SRC驅動多個sink groups的forest候選。兩附件提供不同合法拓樸，修正不必限定為其中一種。每個trial需隔離occupied/tree、完整檢查limits/positions/連通，保留現有合法候選及Score基準，限制node count、候選數及全局建構工作量，避免全chip或無界拓樸窮舉。

此issue與001 unary source forest不同：library可分支，group各含兩sinks；與009完整候選同分的後處理不同，本例尚無任何可用完整候選。

## 重現與驗收

```sh
./tmp/tester/review-51e094e/build/cbi 'issues/opened/016 局部建構候選無回退漏掉小型合法樹/input001.cbi' tmp/tester/feasible-result.cbi
python3 'issues/opened/016 局部建構候選無回退漏掉小型合法樹/verify_witness.py'
```

驗收：原測資成功输出合法樹；sink順序變更、佔位與總length邊界、不合法試次的狀態隔離、較大輸入的建構工作量上限；001–015分數不退步。避免只把錯誤訊息改為另一種無解文字。
