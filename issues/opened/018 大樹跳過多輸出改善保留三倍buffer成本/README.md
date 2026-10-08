# 大樹跳過多輸出改善，保留三倍 buffer 成本

優先級：中（大輸入的成本優化）。檢查來源 commit `fef704e`。

## 反例

chip=64x64；SRC=(32,0)，fanout=1、length=200；1024個sinks為全部 (x,y)，x/y各取1,3,5,...,63，順序為x外圈/y內圈。唯一型別T0：fanout=4、length=200、cost=1；w_skew=100。

原輸出1023個buffers，T_max=T_min=94，Score=1023。附件witness-output001為341個buffers的四元樹，T_max=T_min=94，Score=341（成本降低2/3）。

witness幾何規則：root=(32,32)，向四象限中心分支，第一層offset為(±16,±16)，其後逐層offset(±8,±8)、(±4,±4)、(±2,±2)，最後接offset(±1,±1)的sinks。SRC→root長32；所有root→sink路徑長32+16+8+4+2=62，arrival94。各branch outgoing totals依序128/64/32/16/8，均<=200，fanout全4。buffers皆嚴格內部、互不重疊且不占sink奇數格點；全部input sinks唯一parent與levels正確。

Score341達下界：SRC只有1個child，每個buffer最多4個children；B個buffers的樹最多有1+3B個sink leaves。1024sinks需要B>=ceil((1024-1)/3)=341。每buffer cost1，skew非負，Score>=341。

## 原因與建議

binary Builder固定兩子分支；build_star無法由fanout1的SRC接1024sinks，hub也無法由fanout4型別接全部sinks。原binary tree總元件2048>512，所有improve_tree/relocate_tree等局部改善及多候選refinement都依門檻跳過，所以即使library可多輸出，最後保留1023個二元buffers。此資料目前執行約0.019秒，已有成本優化空間，而不需放開全部昂貴搜尋。

建議加入能處理大樹的有界多輸出建構或可擴展的局部合併：依可用fanout產生多way空間分組候選，或利用父子fanout/总length/type compatibility先篩選少量合併，再用全樹實際Score驗證與保留舊結果。可重用已知subtree metrics/增量更新，避免每個node×全部trials重新複製驗證整棵樹。不要只是刪除512元件門檻、讓所有高成本局部搜尋無界運行。

新大樹策略應有自己的linear/quasilinear候選/工作量控制；檢查所有buffer嚴格interior/佔位、source及parent總線長、型別重選、sink arrival與skew，並在與舊binary候選比較後只採更低合法Score。此issue與008小型bypass不同：008已通過，但對大樹整段被門檻跳過。

## 重現與驗收

```sh
./tmp/tester/review-fef704e/build/cbi 'issues/opened/018 大樹跳過多輸出改善保留三倍buffer成本/input001.cbi' tmp/tester/large-multiway-result.cbi
python3 'issues/opened/018 大樹跳過多輸出改善保留三倍buffer成本/verify_witness.py'
```

驗收：原輸出合法Score341；fanout2/不同library長度與cost組合仍合法且不退步、非完整四象限/不等數量/佔位/邊界點的回退、大輸入時間與memory控制；001–017分數不退步。原建構與全樹後處理不應被新策略較差結果覆寫。
