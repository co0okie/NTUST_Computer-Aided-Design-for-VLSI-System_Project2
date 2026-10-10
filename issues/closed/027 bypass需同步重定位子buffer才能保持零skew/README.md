# bypass 需同步重定位子 buffer 才能保持零 skew

檢查版本：`f484b34`、`04a3261`。此例需要兩個 buffers，單-buffer子集候選不能達到驗收分數；是刪除祖先branch時需同步調整children幾何的缺口。

## 四個 sinks 的反例

9×9，SRC=(5,3)，fanout=4、總length=6；唯一buffer fanout=2、length=14、cost=3，w=34。
S1=(6,1)、S2=(4,9)、S3=(0,3)、S4=(6,3)。

目前三buffer解：B1=(4,3)、B2=(4,5)、B3=(4,4)，SRC→B3，B3→{B1,B2}，B1→{S3,S1}，B2→{S4,S2}。T_max=T_min=7，cost=9，Score=9。

合法witness保留兩個leaf buffers的sink分組，只移除B3並搬動兩者：

- B1=(3,2)，B2=(5,6)，SRC→{B1,B2}。
- SRC outgoing=3+3=6，兩buffers outgoing都為4+4=8≤14。
- 所有sink delays仍為7，cost=6，Score=6。

原序及反序皆實際9、witness6，附兩份完整輸入/輸出。

## 卡住的單一步驟

直接bypass B3，保持B1/B2原位置：SRC outgoing=1+3=4合法，但B1下arrival=5、B2下arrival=7，Score=6+34×2=74，故被拒絕。

先把B1單独搬到witness位置，或先把B2搬到witness位置，都使一側arrival=9、另一側7，Score=77，仍被拒絕。兩個leaf buffers同時搬動可保持zero skew、Score9，屬於平分移動，現有strict-improvement搜尋不接受；再bypass才會減到6。

與013「後續已改善後重試bypass」不同，本例單一步驟沒有改善，重新dispatch既有operators無法跨過這個平台。

## 6 是全域最優值

SRC到S2的最短距離為7；其他三個sinks直連delay分別為3、5、1。因此任何zero-skew解中，其他三個sinks都不能直接連SRC。只有一個fanout2 buffer時無法同時覆蓋這三個sinks，故zero-skew至少需要兩buffers，cost≥6。任何非零skew的Score至少34>6。witness達到全域下界6。

## 可探索方向及重現

可以把bypass與受影響children的一組有限重定位合成一個候選，直接驗證最後拓樸/weighted Score；或增加獨立的雙buffer source-forest幾何候選。每個vicinity點與組合都需有共享work上限，保留原合法結果。此例不需要修改sink分組或接受更差中間解。

```sh
python3 'issues/opened/027 bypass需同步重定位子buffer才能保持零skew/repro/verify.py' build/cbi
```

checker獨立驗證witness、solver全部限制與stdout，Score≤6才通過；輸出放`tmp/tester/027/`。未修改程式，未執行grader。
