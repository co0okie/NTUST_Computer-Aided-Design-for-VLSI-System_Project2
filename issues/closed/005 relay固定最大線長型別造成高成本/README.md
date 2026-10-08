# relay 固定最大線長型別造成高成本

優先級：中（Score 優化，附件降低 94%）。檢查 commit：`6b98dc2`。

## 反例

SRC=(2,2)，sink=(32,2)，chip=40x40；SRC fanout=1、length=1；library：

| 型別 | fanout | length | cost |
| --- | --- | --- | --- |
| T0 | 1 | 5 | 1 |
| T1 | 1 | 100 | 100 |

w_skew=1。只有一個 sink，因此 skew 必為 0，Score 只受 buffer cost 影響。

目前 `actual-output001.cbi`：SRC->B1=(3,2)->S1，B1 用 T1，arrival=30，Score=100。

附件 `witness-output001.cbi`：六個 T0 位於 (3,2),(8,2),(13,2),(18,2),(23,2),(28,2)，最後接 S1=(32,2)。SRC edge=1，中间 edge=5，最後 edge=4；全部 outgoing limits 符合，元件內部且互異，arrival=30，Score=6。

此例 Score=6 亦為下界：不用 T1 時，k 個 T0 的路徑總長至多 1+5k，至少需涵蓋 SRC 到 sink 的 Manhattan 距離 30，因此 k>=6；使用任一 T1 則 cost>=100。

## 原因與建議

Builder 建構時 relay_type 固定取 library 最大 length；route 插入一個 relay 後將下一段 budget 設為該型別的 length。finish 雖逐 buffer 選最便宜相容型別，但原有 edge 長 29，T0 無法驅動，不能得到六個便宜 relay 的方案。hub 完整 Score 搜尋也共用同一路由，因此沒有考慮這種 spacing/type tradeoff。

建議比較不同 relay 型別／間距政策的完整樹（至少保留最大線長與低成本方案），或對 unary chains 使用帶 buffer cost 的動態規劃／有限候選路徑搜尋。不能只挑最便宜型別或最小 cost/length 比值，最後一段、佔用點、boundary detour、SRC budget 與 component/time budget 都會影響結果。維持完整 validate_tree 與真實 Score 驗證。

## 重現與驗收

```sh
cmake -S . -B tmp/tester/relay-cost-build -DCMAKE_BUILD_TYPE=Release
cmake --build tmp/tester/relay-cost-build -j2
./tmp/tester/relay-cost-build/cbi 'issues/closed/005 relay固定最大線長型別造成高成本/input001.cbi' tmp/tester/relay-cost-output001.cbi
python3 'issues/closed/005 relay固定最大線長型別造成高成本/verify_witness.py'
```

驗收：附件合法且 Score=6，保持 arrival=30；增加「長型別確實更划算」、最後一段不足一個 length、relay 位置被 pin 佔用的對照，並核對既有 hub/multi-relay 測試沒有退步。不要執行 grader。
