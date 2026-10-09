# 022 最終結果再跑既有 delay 可將 Score 50 降至 34

優化建議：`b7df4d7` 的最終輸出合法，但既有 `delay_tree` 在此輸出上再執行一次即可降低 Score。只需809個候選嘗試與2次插入，未達2048/16的有界設定。不是要求無限迭代或全域最佳解。

## 反例與合法 witness

`repro/input001.cbi`：40×40，SRC=(12,28)，10sinks；source fanout=3、總長78，單一T0=(fanout4,length84,cost1)，w_skew=9。

| 結果 | T_max | T_min | 成本 | Score |
| --- | ---: | ---: | ---: | ---: |
| 当前 actual001.cbi | 49 | 44 | 5 | 50 |
| 再執行 delay 的 witness001.cbi | 49 | 46 | 7 | 34 |

witness保留全部原5個buffers座標與拓樸，只增加兩個unary buffers：

- B4→S1 原直接線長12；改B4(29,27)→B6(28,26)→S1(34,20)，長2+12=14，S1到達時間44→46。
- B2→B3 原長7；改B2(23,31)→B7(22,30)→B3(28,29)，長2+7=9；B3下面S6/S10的arrival由46/45增加到48/47。

其他sinks不變，最大arrival仍49，最小由44→46；新增成本2，skew收益18，Score净降16。新增buffer位置唯一且嚴格interior、T0允許unary，改動後所有出線**總**長/degree均合法，.level已按新深度重新序列化。附帶actual/witness已完整獨立checker驗證。

## 調整方向與驗收

目前 `refine_tree` 的delay phase在subtree-swap phase之前完成，而最後phase包含subtree swap/bypass/relocation/leaf swap/reparent、沒有delay。證據只表明最終結果重新執行delay有改善；並未證明某一個stage是唯一根因，也可能涉及替代候選slice budget或有限pass。

可考慮保留現有winner後，增加有明確總trial及insertion cap的最後delay/相關局部搜尋重訪；或調整有限phase順序/預算配置。不指定實作，也不建議無限制反覆refine。最終選擇以完整合法結果的actual weighted Score為準，保留原結果避免退步。

驗收本例合法輸出Score≤34，001–021分數不退步，保持搜尋/插入/時間上限。附witness是已知可達門檻，並非全域最優證明。

## 重現

`python3 issues/opened/"022 最終結果漏掉既有delay改善"/repro/verify.py <solver binary>`

附revisit_delay.cpp可用 `c++ -std=c++17 -O2 -I . <harness> -o tmp/tester/revisit_delay` 編譯；參數input、actual、witness。harness呼叫現有solve，再呼叫delay_tree（trial budget2048、insertion budget16），stdout報50 34，stderr報809 trials/2 insertions。正常CLI当前也報Score50。沒有修改src/tests，沒有執行grader。
