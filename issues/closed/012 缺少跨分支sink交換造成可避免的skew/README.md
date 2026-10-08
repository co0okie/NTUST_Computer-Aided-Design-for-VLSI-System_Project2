# 缺少跨分支 sink 交換，造成可避免的 skew

優先級：中（固定元件的連線優化）。檢查來源 commit `7ab6560`。

## 反例

chip=30x30；SRC=(25,28)，fanout=3、length=34；sinks依序 S1=(11,10)、S2=(28,8)、S3=(23,15)、S4=(16,5)、S5=(10,28)。唯一型別 T0：fanout=2、length=31、cost=1；w_skew=12。

原輸出4個buffers：B1=(20,8)、B2=(10,15)、B3=(9,15)、B4=(10,9)。連線 SRC→B4、B4→{B1,B3}、B1→{S4,S2}、B3→{S1,B2}、B2→{S5,S3}。

witness 只交換 S1/S4 的 parent：B1→{S1,S2}、B3→{S4,B2}；所有pin與buffer座標、buffer數量、型別及其他連線完全保留。

| 輸出 | S1 | S2 | S3 | S4 | S5 | skew | cost | Score |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| actual-output001 | 48 | 53 | 55 | 52 | 55 | 7 | 4 | 88 |
| witness-output001 | 56 | 53 | 55 | 58 | 55 | 5 | 4 | 64 |

交換後 SRC outgoing length=34<=34、B4=18<=31、B1=19<=31、B3=18<=31、B2=26<=31；所有buffers嚴格在chip內且座標唯一，parent fanout、連通性、level與編號皆合法。Score改善24（約27%）；此處只證明有較好合法解，未主張64為全域最優。

## 原因與建議

建構階段的spatial partition固定子樹membership；既有bypass/relocation只改合併及位置，不把sink從一個分支換到另一個分支。此例重新分配兩個leaf的parent便能降低全樹arrival範圍，且不需增加元件。

建議加入有界的leaf交換搜尋，先處理不同parent的sink pairs，再依必要擴展到子樹交換。保持每個sink恰有一個parent與全部原pin位置；交換後重選合法便宜buffer型別、完整驗證parent總outgoing length、arrival、weighted Score與serialize層數，僅採嚴格改善。若擴展到buffer子樹交換須避免祖先/子孫交換形成cycle。限制tree size、候選pairs、passes與總評估次數，保留原候選/bypass/relocation結果及預算；不要對大規模輸入無界做全pairs×全樹驗證。

此issue與011不同：witness完全沒有移動buffer，只改兩個sink的parent；與008刪除多餘branch buffer也不同，本例元件數量不變。

## 重現與驗收

```sh
./tmp/tester/review-7ab6560/build/cbi 'issues/closed/012 缺少跨分支sink交換造成可避免的skew/input001.cbi' tmp/tester/swap-output001.cbi
python3 'issues/closed/012 缺少跨分支sink交換造成可避免的skew/verify_witness.py'
```

驗收：附件合法且Score<=64；覆蓋交換後超出parent總length的拒絕案例、sink順序改變、不同depth的leaf若有採樣須重新serialize正確level、巨大dimensions/較多sinks的工作量上限；001–011既有Score基準不退步。
