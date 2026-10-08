# 分支總線長平均分配漏掉較低 relay 成本

優先級：中（離散 relay 成本優化）。檢查 commit `6b4fac3`。

## 反例

SRC=(3,3)、fanout=1、length=100；sinks=(20,10),(20,30)；chip=40x40；w_skew=1。

| 型別 | fanout | length | cost |
| --- | --- | --- | --- |
| T0 | 2 | 6 | 5 |
| T1 | 1 | 6 | 1 |

actual-output001：分支 B1=(20,20)，兩個 child 首段各3；兩側各需兩個 unary relays。T_max=T_min=44，branch cost5 + relay cost4，Score=9。

witness-output001：保留 B1=(20,20)，首段分成2+4。左側 B2=(20,18)->B4=(20,12)->S1，右側 B3=(20,24)->S2。分支總 outgoing length=2+4=6，unary後續長度6、2與6均合法。只需三個T1；T_max=T_min=44、Score=8。

此例 Score=8 達下界：SRC fanout=1且有兩個sinks，至少一個T0（cost5）；若有兩個T0成本已>=10。只有一個T0且至多兩個T1時，全部 buffers 的 outgoing budgets 總和<=18，無法覆蓋兩sink距離20的樹內路徑。因此至少三個T1，總cost>=8，skew非負。

## 原因及建議

cluster 首先 ba=limit/2、bb=limit-ba；只在某個child距離小於其budget時釋放餘額。本例兩個child距離均10，所以固定3/3。每側剩餘距離7需要ceil(7/6)=2個relay；若分成2/4，剩餘8/6只需2+1個。既有 relay DP 可以最佳化單條chain，但沒有聯合決定兩個首段budget。

建議對分支budget做有界分配搜尋／DP，使用各chain的coverage cost作下界或候選評估，再完成routing/驗證真實Score。首段位置佔用、型別重選與boundary detour須在實際候選驗證處理；保留平均分配回退。不要按可能很大的limit直接配置線性陣列，可優先考慮型別capacity的跳點、chain成本轉折與小budget窮舉，限制總工作量。

本 issue 與001的SRC star排序不同：只有一棵兩sink的branch子樹，分支型別與拓樸固定，缺口是buffer自身的總length在兩child間分配。也不是006的單chain混合型別問題，本例兩型別length完全相同。

## 重現與驗收

```sh
cmake -S . -B tmp/tester/branch-budget-build -DCMAKE_BUILD_TYPE=Release
cmake --build tmp/tester/branch-budget-build -j2
./tmp/tester/branch-budget-build/cbi 'issues/closed/010 分支總線長平均分配漏掉較低relay成本/input001.cbi' tmp/tester/branch-budget-output001.cbi
python3 'issues/closed/010 分支總線長平均分配漏掉較低relay成本/verify_witness.py'
```

驗收：附件合法且Score=8；補children順序交換、小分支距離釋放budget、理想relay被pin佔位、巨大length的工作量回退；維持既有Score基準。
