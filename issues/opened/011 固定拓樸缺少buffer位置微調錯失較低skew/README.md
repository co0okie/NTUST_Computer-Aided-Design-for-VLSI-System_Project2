# 固定拓樸缺少 buffer 位置微調，錯失較低 skew

優先級：中（位置優化）。程式來源 commit `bf97d1d`；提出時 HEAD `eba76ac`，src/tests/CMakeLists 與 bf97d1d 相同。

## 反例

chip=30x30；SRC=(26,8)，fanout=1、length=19；四個 sinks 依序為 (12,25)、(29,2)、(15,23)、(26,14)。唯一型別 T0：fanout=2、length=22、cost=10；w_skew=1。

原輸出三個 buffers B1=(13,6)、B2=(24,6)、B3=(13,17)，連線為 SRC→B1、B1→{B2,B3}、B2→{S2,S4}、B3→{S1,S3}。

| 輸出 | S1 arrival | S2 arrival | S3 arrival | S4 arrival | cost | Score |
| --- | --- | --- | --- | --- | --- | --- |
| actual-output001 | 35 | 35 | 34 | 36 | 30 | 32 |
| witness-output001 | 35 | 35 | 36 | 36 | 30 | 31 |

witness 只把 B3 移至 (12,15)，其餘位置、型別、拓樸完全一致。SRC outgoing length=15<=19；B1 outgoing length=11+10=21<=22；B2=9+10=19<=22；B3=10+11=21<=22。所有 buffers 嚴格位於內部且座標唯一，fanout/連通性/level 皆合法。

Score31 達到下界：SRC fanout1、四 sinks、每buffer fanout至多2，所以至少3個buffers，cost>=30。任意 Manhattan 路徑長的奇偶固定為端點座標和的奇偶差；S1/S2 座標和為奇數、S3/S4 為偶數，因此 arrival 不可能全部相等，skew>=1。

## 缺口與建議

`merge_position` 在自底向上建構時根據子樹 virtual arrivals 選點；`improve_tree` 目前只嘗試刪除 child buffer、合併其 children，或把 parent 搬到被刪 buffer 的位置。這些步驟無法單獨調整 B3，雖然成樹後的上游路徑與另一子樹已提供更準確的 skew 目標。

建議加入有界的固定拓樸位置改善：從小範圍鄰域、parent/children 座標與 Manhattan 等時/容量邊界生成候選；檢查佔位、嚴格 interior、parent 與自身總 outgoing length，重選合法便宜型別，再以整棵樹實際 Score 比較。保留原候選及既有改善預算，嚴格改善才採用。限制節點數、候選數與總評估次數；不要按整個 chip 網格掃描，也不要因移位漏算上游邊長或假定子樹 arrival 不變。

這與010不同：本例沒有 unary relays，也沒有改分支首段預算或型別，只改一個既有分支的位置。

## 重現與驗收

```sh
./tmp/tester/review-bf97d1d/build/cbi 'issues/opened/011 固定拓樸缺少buffer位置微調錯失較低skew/input001.cbi' tmp/tester/relocation-output001.cbi
python3 'issues/opened/011 固定拓樸缺少buffer位置微調錯失較低skew/verify_witness.py'
```

驗收：原測資合法輸出 Score31；交換 sinks 的順序、pin 佔用理想位置、接近 chip 邊界、巨大 dimensions 的工作量限制；既有001–010案例分數不退步。
