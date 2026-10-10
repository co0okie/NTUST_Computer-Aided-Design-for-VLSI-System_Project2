# sink 分組與 buffer 位置需同步改動才能降低 skew

檢查版本：`f484b34`。與024的「全部 sinks 共用一個 hub，位置候選漏點」不同，本例必須改變 SRC 直連的 sink 與 buffer 下的 sink 分组，同時移動 buffer；所有既有單一步驟都卡住。

## 三個 sinks 的最小反例

15×15，SRC=(2,12)，fanout=3、總length=23；單一 buffer 型別 fanout=3、length=16、cost=6，w=3。S1=(2,2)、S2=(13,11)、S3=(6,7)。

實際：B1=(7,11)，SRC->{S1,B1}，B1->{S3,S2}。sink delays=10、12、11；cost=6，Score=12。

合法 witness：B1=(1,6)，SRC->{B1,S2}，B1->{S1,S3}。

- SRC outgoing=7+12=19 ≤23。
- buffer outgoing=5+6=11 ≤16，fanout=2 ≤3。
- sink delays=12、12、13；cost=6，Score=9。

反轉 sink 順序同樣實際12、witness9，附兩個輸入及輸出。

## 為何單一 operator 無法改善

在實際拓樸中 S1 直連的 delay 固定10，而 S2 的任何路徑至少為 Manhattan distance 12。因此只移動 buffer 仍 skew≥2，無法嚴格降低 Score。

在B1=(7,11)固定時：

- 把S1與S2交換會令 buffer outgoing=14+5=19 >16；S1/S3交換則為14+6=20 >16。
- 把S1改接到buffer會 outgoing=25 >16。
- 把S2或S3改接SRC會 outgoing=28或25 >23。
- bypass buffer使SRC總length=10+12+9=31 >23。
- 增加delay relay至少多cost6；mixed parity仍需skew≥1，故新增buffer後Score至少15，不能改善目前12。

必須聯合更換直連sink與buffer位置，或獨立產生新的部分hub分組候選，才能跨越這個局部最佳。

## 9 是全域最優值及建議

直接連三個sinks總length31不合法，因此至少一個buffer、cost≥6；sink parity混合，skew≥1。因此Score≥6+3=9，witness達到下界。

可對小型SRC分支保留有界的「單sink交換/改接 + 受影響buffer重新取點」候選，或在少量sinks時直接列舉單buffer的sink子集合，再套用有界幾何取點。不需接受較差中間解或無界grid掃描；每個聯合候選直接完整驗證並依真正Score接受。

## 重現

```sh
python3 'issues/opened/025 sink分組與buffer位置需同步改動才能降低skew/repro/verify.py' build/cbi
```

checker獨立驗證witness與solver合法性，Score≤9才通過。暫存輸出在`tmp/tester/025/`；未修改程式，未執行grader。
