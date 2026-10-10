# hub 取點漏掉 SRC 可達區域與 sink 平衡座標交點

檢查版本：`8d47e51` 與 `f484b34`。兩個 sinks 即可重現；不是要求對一般問題做無界最優搜尋。

## 實際結果與合法 witness

14×14 晶片，SRC=(0,2)、fanout=1、總 length=5；S1=(6,13)、S2=(3,13)，w=26。

buffer library：fanout 都為2，短型 length=17/cost=8，長型 length=41/cost=12。

目前 solver 使用兩個短型 buffer，T_max=18、T_min=17、cost=16，Score=42。

`witness001.cbi` 僅用一個長型 buffer 放在(4,2)：

- SRC outgoing length=4 ≤5。
- buffer outgoing length=13+12=25 ≤41，fanout=2。
- sink delays=17、16，cost=12，Score=12+26×1=38。

`input002.cbi` 對調 library 順序，仍得到42；對應 witness 同為38。反轉兩個 sinks 的輸入順序也仍為42。x/y 全部互換後 solver 可得到38，顯示這個有限候選集合還有軸向不對稱。

## 38 是此例的全域最優值

兩個 sinks 的 Manhattan parity 不同，任何整數座標的路徑 skew 至少為1。SRC fanout=1，至少需要一個 branching buffer。

若只用一個短型 buffer，其 SRC incoming distance ≤5。由三角不等式，buffer 到兩 sinks 的 outgoing sum 至少為 `(17+14)-2×5=21`，超過短型的17，所以不合法。

一個合法 buffer 的最低 cost 因此至少12；兩個以上 buffers 的 cost 至少16。任何解 Score ≥12+26=38，witness 正好達到此下界。

## 候選缺口及建議

hub 候選目前在 sink median、sink bounding-box center 與 SRC 附近各取半徑2。這個例子 sink 的平衡 x=4/5 靠近 median，但要搭配 SRC 的 y=2；兩組座標沒有交叉組合，SRC 邊界還會把 near-source 中心 clamp 到x=1，半徑2到不了x=4。

可在原有有界候選之外加入 median/center x 搭配 SRC y，以及 SRC x 搭配 median/center y，或將平衡線投影到 SRC length diamond。這只新增少量幾何候選，避免全晶片掃描。另一方向是合併 relay/branch 時聯合重定位，因為在原位置做 bypass 會違反 SRC length。

## 重現

```sh
python3 'issues/opened/024 hub取點漏掉SRC可達區域與sink平衡座標交點/repro/verify.py' tmp/tester/review-f484b34/cbi
```

可將 binary 換成自行建置的 `build/cbi`。獨立 checker 驗證 witness 與 solver 全部限制及其 stdout；合法且 Score≤38 才通過。執行輸出放 `tmp/tester/024/`，不執行 grader，也未修改程式。
