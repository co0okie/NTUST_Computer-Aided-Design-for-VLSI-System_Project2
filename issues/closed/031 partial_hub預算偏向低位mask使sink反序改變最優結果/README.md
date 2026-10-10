# partial_hub 預算偏向低位 mask，使 sink 反序改變最優結果

檢查版本：`c6c8894`、`7d87ec4`。025的聯合membership/geometry候選已能求解三sinks，但在它聲明支援的八sinks內，固定低位mask枚舉與共享8192quota仍會讓後段關鍵group沒有得到嘗試。

## 兩份只有sink順序不同的輸入

20×20，SRC=(2,12)，fanout=8、length=83；唯一buffer fanout=3、length=16、cost=6，w=3。

六個far sinks的SRC直接distance都為12；另外兩個near sinks為(2,2)、(6,7)，放在輸入最後兩位S7/S8。

- `input001`：實際合法Score12，Tmax12、Tmin10、cost6。
- `input002`：把8 sinks完全反序後，實際合法Score9。
- 同一合法witness：B1=(1,6)，buffer只驅動兩個near sinks，其餘六個直接SRC。SRC outgoing=7+6×12=79≤83，buffer outgoing=5+6=11≤16；所有delay為12或13，cost6，Score9。

直接SRC總length=91>83，故至少一個buffer；sink parity混合，skew≥1。因此Score≥6+3=9，witness正好全域最優。

## 區分geometry漏點與quota漏點

兩near sink的原順序group mask為192；反序後為3。partial_hub先full mask再依mask整數遞增試每組所有point；大量較早無用/非法point也消耗共享quota，後面group沒有公平分配slice。

診斷用`tmp/tester/quota-probe/`完整複製7d87ec4源碼，只把partial_hub的8192改成262144；相同input001即得到Score9，完全未改points或mask算法。這證明原有geometry候選已含合法最優點，缺口是工作分配順序。原repository程式未修改，probe不構成建議直接擴大全域預算。`repro/check_quota.py`可從7d87ec4重建此診斷binary（所有改動都在忽略的tmp/tester/031），再跑同一獨立checker。

## 建議及重現

可按座標/arrival deficit排序group而不是pin ID，或以round-robin／每group初始slice分配同一8192work，保留full hub的獨立優先slice，再在有希望的group追加工作。保留現有完整結果及所有geometry/實際Score驗證。並用sink排列測試，避免對每個新反例只特判某個mask。

```sh
python3 'issues/opened/031 partial_hub預算偏向低位mask使sink反序改變最優結果/repro/verify.py' build/cbi
```

獨立checker驗證witness/solver全部限制及stdout，兩份皆合法且Score≤9才通過。輸出在`tmp/tester/031/`；未修改程式，未執行grader。
