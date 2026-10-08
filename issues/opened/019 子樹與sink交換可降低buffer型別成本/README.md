# 019 子樹與 sink 交換可降低 buffer 型別成本

優化建議；目前輸出合法，但交換兩條分支中的子樹根後可得到較低 Score。針對 commit `c564752`（目前 HEAD `624401d` 僅記錄 issue 018 驗收，src/tests 相同）重現。

## 反例

`repro/input001.cbi`：30×30，SRC=(4,17)，8 個 sinks，SRC fanout=4、總輸出線長上限=16；T0=(fanout 2, length 17, cost 7)、T1=(2,66,13)，w_skew=15。

| 輸出 | T_max | T_min | 成本 | Score |
| --- | ---: | ---: | ---: | ---: |
| actual001.cbi | 32 | 29 | 67 | 112 |
| witness001.cbi | 32 | 29 | 61 | 106 |

原輸出的相關分支：`B6:{B4 B5}`，`B5:{S3 S6}`。B4 本身有兩個 sink 子節點。交換 B4 與 S3 後變成 `B6:{S3 B5}`、`B5:{B4 S6}`；所有節點座標不變。B6 總輸出線長從 8 變 10，仍可用 T0；B5 從 20 變 16，因此由 T1 改成 T0，省下 6。SRC 總輸出線長仍為 8。

S1…S8 arrival 原為 `[29,30,32,30,29,32,30,31]`，交換後為 `[29,30,30,30,29,32,30,31]`。最大最小值保持 32/29，因此 Score 降 6。witness 深度由 4 變 5，已重新序列化正確 `.level`。

## 建議方向與驗收

現有 `swap_leaves` 僅允許 sink↔sink。可考慮小樹的有界 subtree-root swap：兩個不同 parent 的 child root，至少一個是 buffer；先排除 root 互為祖先（避免環），在副本交換兩條邊，再重選 buffer 型別並完整驗證，只有實際 Score 下降才採納。

本例兩個 parent 本身有祖先关系（B6 是 B5 的 parent），但所交換的 B4 與 S3 互非祖先；不要過度排除這種合法交換。單次 leaf reparent 會讓 fanout=2 的 B6 變 3，單次 subtree reparent 也會讓 B5 變 3，因此需要同步交換。buffer 數不變、沒有空 buffer。

驗收：本例輸出完整合法且 Score ≤106，保留既有案例；新增候選應有與現有 local search 相同等級的工作上限。不要求達成全域最優，也不要求照特定方式實作。優化後仍須確認 parent/child 關係、無環、連通、fanout、**總**輸出線長、座標、sink 到達時間和序列化層級。

## 重現

`python3 issues/opened/"019 子樹與sink交換可降低buffer型別成本"/repro/verify.py <solver binary>`

verify.py 會独立驗證附帶的 actual/witness，並執行指定 solver；目前 solver 應報 112，附帶合法 witness 为 106。`subtree_swap.cpp` 為尋找 witness 的測試 harness，可用 `c++ -std=c++17 -O2 -I . <harness> -o tmp/tester/subtree_swap` 編譯，參數為 input、baseline output、swap output。所有驗證只使用本專案輸出檢查器，沒有執行 grader。
