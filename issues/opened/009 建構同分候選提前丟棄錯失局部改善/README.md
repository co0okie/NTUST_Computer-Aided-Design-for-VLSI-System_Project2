# 建構同分候選提前丟棄錯失局部改善

優先級：中（候選選擇次序的 Score 優化）。檢查 commit `138f979`，後續 `cbded59` 無程式變更。

## 可重現證據

chip=30x30，SRC=(25,19)、fanout=1、length=37；唯一 T0 fanout=2、length=15、cost=1；w_skew=10。八個 sinks 詳見 input001.cbi。

目前 actual-output001：T_max=34、T_min=33、cost=8，Score=18。

以同一份程式的 Builder/improve_tree 函式建立兩棵不同 parity 候選並改善：

| 候選 | 改善前 Score | 改善後 Score |
| --- | ---: | ---: |
| parity=true | 18 | 18 |
| parity=false | 18 | 17 |

附件 witness-output001 是 parity=false 候選經現有 improve_tree 後的實際輸出：T_max=33、T_min=32、cost=7，Score=17。兩份樹皆經獨立序列化 checker 驗證合法；未聲稱17為全域最優。

## 原因與建議

solve 的 consider 僅在 metrics.score 嚴格小於 best_score 時保留 candidate；順序先 true 再 false，同分 false 被丟棄。直到全部候選篩完後才 improve_tree(best)，所以局部改善功能永遠看不到本來也合法、同分但不同形狀的 false 候選。改成簡單偏好最後一棵同分樹仍會在其他輸入漏掉相反情況。

建議在有界候選集合中保留不同拓樸／位置的同分或接近分數候選，再按改善後完整 Score 決定；或至少對兩種 parity 的最佳候選各自執行 improve_tree。需有總 trial budget／top-k等上限，避免每棵樹都無限制跑局部搜尋。完整驗證仍是必要條件，保留原輸出作回退。

## 重現

repro/compare_candidates.cpp 是 tester 測試 harness，直接包含原程式並呼叫其 Builder/improve_tree，僅比較已有候選；沒有修改 src/，沒有執行 grader。

```sh
cmake -S . -B tmp/tester/candidate-review-build -DCMAKE_BUILD_TYPE=Release
cmake --build tmp/tester/candidate-review-build -j2
./tmp/tester/candidate-review-build/cbi 'issues/opened/009 建構同分候選提前丟棄錯失局部改善/input001.cbi' tmp/tester/candidate-actual.cbi
c++ -std=c++17 -O2 'issues/opened/009 建構同分候選提前丟棄錯失局部改善/repro/compare_candidates.cpp' -o tmp/tester/compare-candidates
./tmp/tester/compare-candidates 'issues/opened/009 建構同分候選提前丟棄錯失局部改善/input001.cbi' tmp/tester/candidate-witness.cbi
python3 'issues/opened/009 建構同分候選提前丟棄錯失局部改善/verify_witness.py'
```

驗收：附件合法且 Score<=17；補 parity 順序交換或類似同分形狀的對照，保留原 Score 基準，並觀察多候選局部搜尋的總工作量／runtime。
