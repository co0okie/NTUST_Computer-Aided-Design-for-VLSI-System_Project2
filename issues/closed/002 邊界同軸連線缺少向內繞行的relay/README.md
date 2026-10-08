# 邊界同軸連線缺少向內繞行的relay

優先級：高（合法測資無輸出）。

基準 commit `625f183` 對附件測資返回 exit 1：

```text
cbi: no legal tree constructed: no free shortest-path relay position found
```

SRC=(0,0)，S1=(10,0)，SRC 與 unary buffer 的 length 都是 3。最短路徑位於 y=0，沒有任何可合法插入的內部 buffer，但可以經內部 y=1 繞行。附件合法解使用 (1,1),(4,1),(7,1),(9,1)，各 outgoing length 分別為 2,3,3,2,2；T_max=T_min=12，Score=4。

規格未要求每條路徑為最短路徑；grader 靜態參考明確要求 buffer 嚴格位於內部（並未執行 grader）。因此不能透過允許 boundary buffer 修正。

原因：relay_position 僅在 parent/target 的 Manhattan 最短路徑上尋找可用點。建議加入向內 detour fallback，注意 detour 改變 arrival，必須更新 cluster 的 low/high，避免仍用原始端點距離估計造成上層平衡偏差。

驗收：附件可輸出合法樹；四邊上的同軸 pins、被佔用的第一個 inward relay、budgets=1/2 的可行與不可行情況均有檢查，且 buffer 不得觸及 boundary。

## 重現與證據

```sh
cmake -S . -B /tmp/cbi-tester-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/cbi-tester-build -j2
/tmp/cbi-tester-build/cbi "issues/closed/002 邊界同軸連線缺少向內繞行的relay/input001.cbi" /tmp/output001.cbi
python3 "issues/closed/002 邊界同軸連線缺少向內繞行的relay/verify_witness.py"
```

`witness-output001.cbi` 是證明可行／較低 Score 的手工解，並非要求 coder 輸出完全相同拓樸。獨立驗證使用 repository 的 test helper；沒有載入或執行 grader。
