# 完整 refinement 前依初始 Score 淘汰可改善候選

優先級：中（候選選擇與後處理協作）。檢查來源 commit `1ed4520`。

## 反例

chip=30x30；SRC=(9,9)，fanout=3、length=32；三 sinks S1=(9,3)、S2=(17,24)、S3=(24,6)；w_skew=10。

| 型別 | fanout | length | cost |
| --- | --- | --- | --- |
| T0 | 2 | 11 | 4 |
| T1 | 2 | 18 | 13 |

目前輸出5顆T0，T_max28、T_min27、cost20、Score30。附件actual-output001為實際樹。

既有Builder(p,false,-1).build_star()的初始Score178；只執行improve_tree仍178，但再執行現有refine_tree即可Score27，T_max23、T_min22、cost17。witness-output001只需要B1 T1=(17,4)、B2 T0=(9,22)：SRC→{B1,B2}、B1→{S3,S1}、B2→S2。

witness driving：SRC outgoing length13+13=26<=32；B1=9+9=18<=18；B2=10<=11。全部fanout合法，buffer座標嚴格內部且唯一，pins/連通性/IDs/level正確。witness較目前Score低3；未宣稱27為全域最優。

## 原因與建議

solve先依建構Score維持最多4棵一般候選及4棵allocation候選，對shortlist只做improve_tree，再選出單一樹進refine_tree。新增加的relocation/reparent/delay/交換等完整refinement只作用於這一棵樹。

此例star初始arrival範圍很大，所以初始Score差；但它包含適合後續改接與幾何改善的拓樸。bypass本身無法降低178，因此它即使留在短名單也不會在目前流程勝出。完整refinement後反而少3顆buffer，得到較好實際Score。

建議用額外的有界預算對少量拓樸/幾何有差異的候選執行更完整refinement，再比較最終Score。候選保留可兼顧初始cost、skew、拓樸類別，避免僅初始weighted Score排序使不同建構路線消失。保留原勝出樹與其完整改善預算；其他候選共用全局工作量/輪次上限、tree size門檻與dedup，不要將全部policy/parity候選各自無界跑完整34816次。並非要求對每棵候選保證全域最優。

與009相關但不是原反例重複：009已處理同分建構候選的bypass，該附件仍正常Score17。本例初始Score明顯較差且bypass無改善，需考慮後來新增的完整refinement對候選排名的影響。

## 重現與驗收

```sh
./tmp/tester/review-1ed4520/build/cbi 'issues/closed/017 完整refinement前依初始Score淘汰可改善候選/input001.cbi' tmp/tester/candidate-result.cbi
c++ -std=c++17 -O2 'issues/closed/017 完整refinement前依初始Score淘汰可改善候選/repro/star_stages.cpp' -o tmp/tester/star-stages
./tmp/tester/star-stages 'issues/closed/017 完整refinement前依初始Score淘汰可改善候選/input001.cbi' tmp/tester/star-stage-
python3 'issues/closed/017 完整refinement前依初始Score淘汰可改善候選/verify_witness.py'
```

harness三階段依序輸出：初始23/6/178、bypass23/6/178、完整refinement23/22/27。驗收：原輸出合法Score<=27；sink順序、不同weight/型別cost對候選選擇、候選去重/預算耗盡保留舊結果，以及128/較大sinks效能限制；001–016分數不退步。
