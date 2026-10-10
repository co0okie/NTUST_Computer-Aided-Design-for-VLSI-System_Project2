# 密集晶片多輸出建構漏掉十六 sink 合法 forest

檢查版本：`8d47e51`。這是合法解建構能力的問題；solver 的有限搜尋仍會漏解，並非宣稱一般問題應保證求到最優。

## 可重現結果

4×4 晶片，SRC=(4,3)，SRC fanout=3、總 length=10；單一 buffer 型別 fanout=9、總 length=16、cost=1，w=1。

- `repro/input001.cbi`：16 sinks；solver exit 1，`no legal tree constructed: library cannot drive all sinks from one hub`。
- `repro/witness001.cbi`：合法三-buffer 解，T_max=7、T_min=3、cost=3，Score=7。
- `repro/input002.cbi`：同一布局擴到21 sinks、所有25個整數座標均由 SRC/sinks/三個buffer占用；仍 exit 1。`witness002.cbi` 是合法三-buffer 解。

兩個 witness 使用 B1=(1,3)、B2=(3,2)、B3=(1,1)。SRC 到三者的線長分別為3、2、5，總和恰為10；每個 buffer 的 fanout、總 outgoing Manhattan length、內部座標、唯一位置、樹結構與延遲均由獨立 Python checker 驗證。

## 原因及可探索方向

新 multiway 建構固定依空間 median 切割 sinks，再對各組獨立取 merge 點；密集布局中，組別與剩餘可用 buffer 座標必須一起考慮。此例可由三個少量 free interior points 驅動合法 forest，但固定分組/單點選擇沒有保留替代狀態。既有小 forest fallback 只涵蓋至多12 sinks，因此無法補回這兩例。

建議在小面積或剩餘 interior 座標很少時，使用有界可用座標候選，嘗試重新指派 sinks/換 source 分組，並保留不同合法部分 forest。可依「free interior slots 數」限制搜尋工作量，避免只依 sink 數排除此類容易列舉 buffer 位置的問題；不需要無界擴大全域搜尋。

## 重現

```sh
cmake -S . -B tmp/tester/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build tmp/tester/build -j2
python3 'issues/opened/023 密集晶片多輸出建構漏掉十六sink合法forest/repro/verify.py' tmp/tester/build/cbi
```

checker 先獨立驗證 witness，再執行 solver 並驗證其實際輸出；失敗 exit 1。輸出放 `tmp/tester/023/`，不執行 grader。未修改 solver。
