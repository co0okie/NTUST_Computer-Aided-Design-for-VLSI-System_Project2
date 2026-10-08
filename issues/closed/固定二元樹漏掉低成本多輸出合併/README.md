# 固定二元樹漏掉低成本多輸出合併

優先級：中（明顯 Score 優化機會）。

基準 commit `625f183` 對附件測資輸出四個 T0 buffer：T_max=9、T_min=9、Score=40。附件的一個 buffer 解達到 T_max=T_min=5、Score=10，減少 75%。

SRC=(5,1)，fanout=1、length=4；四個 sinks 圍繞 (5,5)，各距離 1；T0 fanout=4、length=20、cost=10。SRC 到 B1=(5,5) 的距離為 4，B1 總 outgoing length=4，所有限制皆滿足。至少需要一個 buffer（SRC fanout=1 且有四 sinks），因此 Score=10 同時是此例的全域下界。

原因：cluster 固定二元切分，即使 library 有足夠 fanout 也不建立多輸出 merge；兩種 parity candidate 只改 arrival offset。完成後僅重選型別，未刪除不必要的 branch/relay。

建議：加入多輸出 star/group candidate，或對已有樹做 buffer bypass/子樹合併，按真實 Score 接受改善。重新核對 SRC fanout、每個 parent 的總 outgoing length、座標佔用與 arrival；不能只看 skew 或個別 edge length。

驗收：附件輸出合法且 Score=10；補上「合併後 fanout 超限」與「每條 edge 合法但總 outgoing length 超限」的對照案例，保留原 candidate 作回退。

## 重現與證據

```sh
cmake -S . -B /tmp/cbi-tester-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/cbi-tester-build -j2
/tmp/cbi-tester-build/cbi "issues/opened/固定二元樹漏掉低成本多輸出合併/input001.cbi" /tmp/output001.cbi
python3 "issues/opened/固定二元樹漏掉低成本多輸出合併/verify_witness.py"
```

`witness-output001.cbi` 是證明可行／較低 Score 的手工解，並非要求 coder 輸出完全相同拓樸。獨立驗證使用 repository 的 test helper；沒有載入或執行 grader。
