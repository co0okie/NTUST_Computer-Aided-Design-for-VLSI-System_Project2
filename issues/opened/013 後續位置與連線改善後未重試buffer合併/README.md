# 後續位置與連線改善後，未重試 buffer 合併

優先級：中（改善階段協作）。檢查來源 commit `83e4791`。

## 反例

chip=30x30；SRC=(13,14)，fanout=3、length=25；六 sinks 依序 (3,13)、(2,2)、(15,17)、(10,19)、(19,5)、(6,28)。唯一型別 T0：fanout=3、length=39、cost=7；w_skew=3。

目前 `solve` 輸出三個 buffers：B1=(9,7)、B2=(10,26)、B3=(14,26)。SRC→{B1,B3}、B1→{S2,S1,S5}、B3→{S3,B2}、B2→{S4,S6}。T_max=24、T_min=23、cost21、Score24。

直接把此完整結果交給既有 `improve_tree`，可刪除 B2，讓 B3 接上 S4/S6；剩下兩個 buffers，重編號後附件 witness 為 B1=(9,7)、B2=(14,26)，SRC→{B1,B2}、B1→{S2,S1,S5}、B2→{S3,S4,S6}。T_max=24、T_min=23 不變，cost14、Score17。

witness 檢查：SRC outgoing length=11+13=24<=25；B1=12+12+12=36<=39；B2=10+11+10=31<=39。buffers皆嚴格位於內部且座標唯一，全部parent fanout<=3，tree/level/IDs正確。

Score17 達到下界：SRC與buffer fanout最多3，一個buffer加SRC最多可提供5個sink leaves，不足6，所以至少2個buffers，cost>=14。sinks座標和有奇數與偶數，任意Manhattan路徑的arrival奇偶由端點固定，因此skew>=1；w_skew=3，Score>=14+3=17。

## 原因與建議

`solve` 依序執行候選建構/`improve_tree`、`relocate_tree`、`swap_leaves`，之後直接return；後兩階段改變位置或membership後，可能創造新的合法buffer bypass，但原合併階段不再重試。附件 harness 使用現有函式：先 `solve(p)`，再 `improve_tree(p,tree)`，無需新move即可24→17。

建議保留原完整結果後，以額外的有界預算在幾個改善階段之間重新嘗試（例如後處理後再合併，或有限輪次依序合併/移位/交換）。每次重新validate實際全樹Score、重編號與serialize；僅採嚴格改善。用全流程總試次/輪次上限控制工作量，停在無改善或預算耗盡，避免無限循環或因分享預算而丟失原勝出結果。不要只针对附件加一次特例。

這與008不同：既有合併演算法已能找到witness，本issue是它在後續改善之後沒有再次被呼叫；也不是011/012缺少新move。

## 重現與驗收

```sh
./tmp/tester/review-83e4791/build/cbi 'issues/opened/013 後續位置與連線改善後未重試buffer合併/input001.cbi' tmp/tester/revisit-result.cbi
c++ -std=c++17 -O2 'issues/opened/013 後續位置與連線改善後未重試buffer合併/repro/revisit.cpp' -o tmp/tester/revisit-check
./tmp/tester/revisit-check 'issues/opened/013 後續位置與連線改善後未重試buffer合併/input001.cbi' tmp/tester/revisit-actual.cbi tmp/tester/revisit-witness.cbi
python3 'issues/opened/013 後續位置與連線改善後未重試buffer合併/verify_witness.py'
```

原commit harness stdout為 `24 17`。驗收：原輸出合法Score17；覆蓋不同sink順序、受parent總length/fanout限制不可合併、改善串接後buffer IDs與level重算、大tree/巨大dimensions工作量上限；001–012分數不退步。
