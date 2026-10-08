# 缺少單一 sink 改接，以使用分支剩餘容量

優先級：中（拓樸鄰域優化）。檢查來源 commit `9233163`。

## 反例

chip=20x20；SRC=(17,2)，fanout=3、length=45；sinks依序 (16,5)、(11,14)、(11,17)、(19,2)、(1,6)。唯一型別T0：fanout=3、length=34、cost=9；w_skew=1。

原輸出 B1=(9,7)、B2=(12,9)；SRC→{B1,B2}、B1→{S5,S2}、B2→{S4,S1,S3}。原arrival S1..S5=(20,22,21,26,22)，T_max=26、T_min=20、cost18、Score24。

witness只把S1改接到B1：B1→{S5,S2,S1}、B2→{S4,S3}。新arrival=(22,22,21,26,22)，T_max=26、T_min=21、cost18、Score23。所有位置、型別、元件數與其他連線完全不變。

改接後 SRC outgoing length=13+12=25<=45；B1=9+9+9=27<=34；B2=14+9=23<=34，fanout分別2/3/2均合法。buffers嚴格在內部且唯一，sink parents、連通性、IDs與level皆正確。此处只證明較佳合法解，未宣稱23為全域最優。

## 原因與建議

`swap_leaves`只交换兩個不同parent的sink，保持兩parent各自child數量；`improve_tree`只刪除buffer，`relocate_tree`只改位置。有限輪次串接這些moves仍不含單一leaf移到另一個有剩餘fanout/length的parent，因此固定了部分leaf membership。

建議加入有界的sink reparent候選：從一個parent移除sink，再接入另一個已有parent（SRC也需遵守其limits），重選最便宜合法型別，完整驗證parent總outgoing length、fanout、全樹arrival與實際Score。僅採嚴格改善。若原parent只剩空buffer，應拒絕或正確刪除/重編號，不能保留未使用buffer；跨depth改接須重新serialize level。leaf改接不會形成ancestor cycle，但需維持唯一parent與全部input pins。

限制tree size、候選數、rounds與總工作量，保留原結果及改善預算；不要無界掃描sink×所有位置/parent。可優先評估剩餘容量足夠且能調整過早/過晚sink arrival的parent，最後仍以實際完整tree作判斷。

此issue與012的兩leaf交換不同：此witness兩parent的child counts由2/3變成3/2，沒有另一個sink反向交換；也不是013缺少再次呼叫現有move。

## 重現與驗收

```sh
./tmp/tester/review-9233163/build/cbi 'issues/opened/014 缺少單一sink改接以使用分支剩餘容量/input001.cbi' tmp/tester/reparent-result.cbi
python3 'issues/opened/014 缺少單一sink改接以使用分支剩餘容量/verify_witness.py'
```

驗收：原輸出合法Score<=23；覆盖parent剩餘fanout不足、總length超限（包括SRC）、移走唯一child的空buffer處理、跨depth改接的IDs/level、sink順序變更與大輸入工作量；001–013分數不退步。
