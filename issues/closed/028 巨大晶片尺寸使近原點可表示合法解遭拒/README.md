# 巨大晶片尺寸使近原點可表示合法解遭拒

檢查版本：`04a3261`。此為目前parser接受的int64數值範圍與既有巨大dimension測試所涵蓋的邊界健壯性問題；不是指一般評分資料必然會使用這種尺寸。

## 只改dimx/dimy就失敗

三份輸入除了晶片尺寸外完全一致，13 sinks都在(10,10)中心、radius4的Manhattan ring上，實際座標介於6與14。

- SRC=(10,9)，fanout=1、length=1。
- buffer library：fanout=13、length=52、cost=1，w=100。
- witness僅B1=(10,10)，SRC→B1→13 sinks；SRC outgoing=1，buffer outgoing=13×4=52，所有sink delay=5，Score=1。
- dim=20：solver合法輸出Score1。
- dim=2^61或2^63−1：solver exit1，`no legal tree constructed: chip dimensions exceed construction range`。

所有pins、buffer、path distances、total outgoing lengths、arrival與Score本身都很小且可精確以int64表示；只多出未使用的空白晶片範圍。附三份合法witness與独立checker。

## 原因及建議

Builder建構前以整個chip dimension超過`INT64_MAX/16`為由全面拒絕，而不是對實際sampled coordinates／距離運算判斷溢位。13 sinks又超过小forest fallback的12-sink截止，因此連這個單hub合法解也無其他建構可補回。

可對有界候選的加减使用checked/saturating arithmetic，逐一丟棄溢位點，而保留仍可表示的局部候選；或為靠近原點的pin cluster使用安全的局部建構範圍，再用原尺寸完整驗證。單純擴大dim只放寬buffer位置限制，理應不能使同一個小座標witness失去合法性。

這不是要求表示超過int64的net或Score，該情況應繼續拒絕。另需保留極端source/budget座標、boundary candidate加减及dim乘積的溢位保護。

## 重現

```sh
python3 'issues/opened/028 巨大晶片尺寸使近原點可表示合法解遭拒/repro/verify.py' build/cbi
```

checker驗證三份witness，執行solver並檢查完整輸出及stdout，三份皆合法Score1才通過；輸出放`tmp/tester/028/`。未修改程式，未執行grader。
