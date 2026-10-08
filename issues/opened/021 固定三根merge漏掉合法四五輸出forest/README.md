# 021 固定三根 merge 漏掉必須使用四或五輸出的合法 forest

正確性問題，承接 issue020 已修正的三輸出案例，這裡提供較高 fanout 的不同合法反例。`d3e7b5f`（HEAD `d27f2d4` 僅記錄020驗收）對附帶三份輸入皆exit1：`no legal tree constructed: library cannot drive all sinks from one hub`。三份正常library的fanout分別4/5/4，附完整合法witness，已由獨立checker驗證。

目前multiway forest只合併2或3roots，不使用library允許的更高fanout。這些密集案例不可能容納max-3-children forest；不是擴大beam或位置採樣便能修復。

| 測資 | sinks | dim | SRC(fanout,length) | T0(fanout,length,cost) | 空的interior位置 | witness buffers | (max,min,cost,Score) |
| --- | ---: | ---: | --- | --- | ---: | ---: | --- |
| input001 | 8 | 4 | (2,2) | (4,5,1) | 2 | 2 | (3,2,2,102) |
| input002 | 10 | 4 | (2,2) | (5,8,1) | 2 | 2 | (3,2,2,102) |
| input003 | 12 | 5 | (3,3) | (4,7,1) | 3 | 3 | (4,2,3,203) |

全部w_skew=100、SRC=(2,2)。input001/002的buffers只能放(1,2),(3,2)：其他7個interior positions被SRC與6個內部sinks佔用，另外2或4個sinks位於合法boundary。input003的12sinks、SRC與3buffers恰好填滿所有16個interior positions，完全不含boundary pins。

witness001兩個buffers各接4個sinks，出線總長各5；witness002各接5個sinks，出線總長各8；witness003三個buffers各接4個sinks，出線總長6/6/7。SRC所有edges各長1，總長2/2/3恰好符合source limit。座標唯一、buffers嚴格interior、無環、連通、無空buffers，.level完整。

## 為何固定arity無法處理

對n sinks、B buffers、SRC fanout最多f，若所有buffers的children最多k，樹的總edge數n+B必須≤f+kB，因此B≥ceil((n−f)/(k−1))。k=3時，三例至少需3/4/5個buffers，分別超過可用空位2/2/3。

input002即使提高到k=4也至少需ceil((10−2)/3)=3個buffers，仍無法容納；k=5時兩個buffers即可。library已有相應正常可用型別，不應只根據固定2/3-root合併失敗就拒絕。

## 建議與驗收

考慮在bounded forest中以library可用fanout選擇較高arity候選，維持總budget/beam/position上限。可以用可用座標容量與剩餘roots的必要branch能力輔助排序或排除不可能的狀態；不指定實作，也不要求窮舉所有subsets。請至少處理上面三個明確合法反例，避免僅把常數3改成4而仍漏掉input002。

驗收：三例均成功且輸出完整合法，不要求特定Score或witness拓樸；保留001–020及既有時間/記憶體上限。仍可使用有界fallback，但應區分搜尋未找到與已證明不可行。

## 重現

`python3 issues/opened/"021 固定三根merge漏掉合法四五輸出forest"/repro/verify.py <solver binary>`

先獨立驗證附帶三份witness，再執行solver。現在三例皆exit1；修復後應正常輸出並通過checker。沒有執行或匯入grader。
