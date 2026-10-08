# 短路徑缺少延遲 relay 插入，而錯失零 skew

優先級：中（增加元件以降低weighted skew）。檢查來源 commit `36f8658`。

## 反例

chip=10x10；SRC=(5,5)，fanout=2、length=100；唯一型別T0：fanout=1、length=100、cost=1；w_skew=100。

input001：S1=(7,5)、S2=(9,5)。目前輸出零buffers，SRC→{S1,S2}，arrivals2/4、Score200。

witness-output001：插入B1=(6,6)，SRC→{B1,S2}、B1→S1。SRC outgoing length=2+4=6<=100；B1 outgoing length=2<=100。兩sink arrival均4，buffer cost1、Score1。所有座標唯一，buffer嚴格在內部，fanout/connected tree/level/IDs合法。

Score1達下界：零buffer的唯一樹是兩個SRC直接edges，Score200；有buffer時cost>=1且weighted skew非負，故Score>=1。

input002奇偶對照：S1=(6,5)、S2=(9,5)，其餘同上。原arrival1/4、Score300；同一個B1=(6,6)使arrival3/4、cost1、Score101。sink座標和奇偶不同，任意Manhattan路徑arrival不能完全相等，skew>=1；零bufferScore300，非零buffer至少cost1，因此101也達下界。

## 原因與建議

現有 `route` 只在連線超出budget時插入relay，star路徑在本例均可直接driving，因此沒有多餘relay。binary分支需要fanout>=2型別，library只有unary；`relocate_tree`/`swap_leaves`/`reparent_leaves`又只操作已有元件，無法從零buffer候選建立短路徑的延遲繞行。現在的成本優化策略沒有比較少量額外cost能否換取更大的weighted skew下降。

建議增加有界的延遲relay候選：對過早到達的leaf或子樹edge，採樣能增加Manhattan路徑長的合法interior位置，將parent→child拆成parent→relay→child，依實際全樹Score決定是否採用。可用目前arrival差、奇偶與Manhattan等長邊界產生候選，避免按大chip網格/大delay掃描。重新驗證parent所有outgoing length、relay fanout/length、pin佔位與元件唯一性，重選合法最便宜型別；不足延遲或需多relay時仍限制總trial/新增元件數，保留舊結果。

只能接受實際weighted Score嚴格改善：低權重/高cost時應保留較便宜直接連線；不同奇偶的sink不可要求不可能的zero skew；也不能把規格的zero intrinsic delay誤認為插入buffer本身有delay，本witness全部延遲來自Manhattan線長增加。

與005/006不同：本例沒有driving超限，需主動加入非最短路徑；與011只改既有buffer位置也不同，本例原解零buffers。

## 重現與驗收

```sh
./tmp/tester/review-36f8658/build/cbi 'issues/opened/015 短路徑缺少延遲relay插入而錯失零skew/input001.cbi' tmp/tester/delay-output001.cbi
python3 'issues/opened/015 短路徑缺少延遲relay插入而錯失零skew/verify_witness.py'
```

驗收：input001合法Score1、input002合法Score101；交換sink順序、低權重高relay成本時保留原結果、parent總length與relay capacity不容許的候選拒絕、理想位置佔用/靠近boundary、巨大dimensions與大arrival差工作量受限；001–014分數不退步。
