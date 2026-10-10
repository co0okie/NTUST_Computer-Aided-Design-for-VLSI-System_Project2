# 長距離混合 relay 未利用 length 公因數縮小 DP

檢查版本：`a06cf91`。這是006已解決短距離混合chain之後的有界延伸優化，並非要求對巨大距離配置線性DP陣列。

## 單 sink、無障礙直線反例

參數scale=k：40k×40k，SRC=(2k,2k)、sink=(31k,2k)，SRC fanout=1、length=k，w=1。

library：T0 fanout1/length8k/cost5；T1 fanout1/length6k/cost4。

- k=1：solver Score18，混合段長8+8+6+6。
- k=1000：solver Score19，relay x=3000/11000/19000/27000，buffer段長8000+8000+8000+4000，型別T0,T0,T0,T1。
- k=1,000,000：同樣Score19，只放大所有坐標與capacity。

合法 witness：relay x=3k/11k/19k/25k，y=2k，型別T0,T0,T1,T1。後續段長8k+8k+6k+6k，總cost18，sink delay=29k，沒有skew。所有buffer內部且互異，所有fanout與length合法。

附兩個放大反例及未放大的正向對照，checker獨立驗證完整樹。

## 18 是全域最優值

SRC最多覆蓋k，buffers至少要覆蓋28k。三個最大length buffers只能覆蓋24k，因此至少四個。四個buffers若只有0或1個T0，總coverage最多24k/26k，仍不夠；至少兩個T0，最低cost=5+5+4+4=18。五個以上buffers最低cost20。因此witness達最優18。

## 有界改進方向

目前`relay_for`以原始remaining distance>4096直接跳過mixed DP。放大後需要將倒數buffer從27k移至25k並改掉前一段型別，固定±2或現有midpoint取點又沒有這個capacity轉折點，故保留19。

所有library length有公因數g=2k。無障礙coverage recurrence可以在`ceil(remaining/g)`及`length/g`上做精確DP；本例只需14個有效distance單位，與k大小無關。實際幾何仍照原單位走route/驗證，既有佔位回退仍需保留。

建議以library length GCD壓縮狀態，再套用原來4096及transition工作上限；g=1或縮放後距離仍大則保留舊fallback。也可新增relay capacity轉折點的少量spacing候選。避免對巨大length線性掃描或取消資源上限。

## 重現

```sh
python3 'issues/opened/029 長距離混合relay未利用length公因數縮小DP/repro/verify.py' build/cbi
```

三份皆合法且Score≤18才通過；輸出放`tmp/tester/029/`，未修改程式，未執行grader。
