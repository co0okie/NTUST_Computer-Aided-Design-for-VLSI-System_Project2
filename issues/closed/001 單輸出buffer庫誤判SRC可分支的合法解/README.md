# 單輸出buffer庫誤判SRC可分支的合法解

優先級：高（合法測資無輸出）。

SRC 可以分支，並不要求 buffer library 一定包含 fanout >= 2 的型別。基準 commit `625f183` 在附件測資返回 exit 1：

```text
cbi: no legal tree constructed: buffer library cannot form a branching tree
```

SRC=(5,5)，fanout=2，總 outgoing length=2；兩個 sinks=(1,5),(9,5)；buffer 只有 fanout=1、length=3、cost=1。附件合法解為 SRC 分別經 (4,5)、(6,5) 到兩個 sinks，arrival 都是 4，Score=2。

原因：`Builder::build()` 對多 sink 強制要求 branching_type，且先把所有 sinks 合成一個根再接到 SRC；direct candidate 沒有為各分支插 relay。因此 SRC 已提供所需分支能力的情況也失敗。

建議：支援 SRC 的多分支 forest，對每個分支路由與分配 SRC 的總 length budget；library 全為 unary 時仍可建構，不宜只是移除檢查後讓 cluster 存取 -1 型別。

驗收：附件成功輸出合法樹，Score=2；另測 SRC fanout 不足、總 outgoing length 限制、各分支需要多個 relay 的案例。保留原有測試通過。

## 重現與證據

```sh
cmake -S . -B /tmp/cbi-tester-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/cbi-tester-build -j2
/tmp/cbi-tester-build/cbi "issues/closed/001 單輸出buffer庫誤判SRC可分支的合法解/input001.cbi" /tmp/output001.cbi
python3 "issues/closed/001 單輸出buffer庫誤判SRC可分支的合法解/verify_witness.py"
```

`witness-output001.cbi` 是證明可行／較低 Score 的手工解，並非要求 coder 輸出完全相同拓樸。獨立驗證使用 repository 的 test helper；沒有載入或執行 grader。
