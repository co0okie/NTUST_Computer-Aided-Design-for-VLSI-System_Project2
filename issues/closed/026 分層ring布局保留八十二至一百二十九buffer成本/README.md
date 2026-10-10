# 分層 ring 布局保留八十二至一百二十九 buffer 成本

檢查版本：`f484b34`。已具備 direct multi-output 建構與大樹 contraction，但不同層需要不同分支數的布局仍有大幅可避免成本；不是只把既有二元樹換成每層固定同樣最大 fanout 就能處理。

## 結果

| 測資 | sinks | 單一型別 fanout/length/cost | 目前 Score / buffers | 合法 witness Score / buffers |
|---|---:|---|---:|---:|
| input001 | 128 | 32 / 256 / 1 | 82 / 82（zero skew） | 5 / 5（zero skew） |
| input002 | 256 | 64 / 1024 / 1 | 129 / 129（zero skew） | 5 / 5（zero skew） |

160×160，SRC=(80,79)，fanout=1、length=1，w=100。四組 sinks 分別在(60,80)、(100,80)、(80,60)、(80,100)為中心的完整 Manhattan ring 上。ring radius 分別為8與16，每組32或64 sinks。

witness 拓樸：SRC→(80,80) root buffer→四個 ring-center buffers→各組 sinks。

- SRC length=1。
- root buffer fanout=4，總length=4×20=80 ≤256/1024。
- 每個 ring buffer fanout=32/64，總length=32×8=256或64×16=1024。
- 所有 buffer 嚴格位於晶片內且位置唯一；ring互不重疊，SRC也未占用sink/buffer位置。
- sink delays 全部為29或37，零skew，總cost=5。

兩份輸入、完整實際輸出、完整合法 witness 及獨立checker均附於repro。

## 5 是全域最優值

SRC fanout=1，buffer fanout=F，任何N sinks的樹至少有 `ceil((N-1)/(F-1))` buffers：本例分別為ceil(127/31)=5與ceil(255/63)=5。每個cost=1且skew≥0，因此Score≥5；witness達到此下界。

## 建構缺口及可探索方向

現有 multiway builder 在各層直接使用 `min(32, nodes.size, type.fanout, type.length)` 的最大分支數，且以空間median固定分組。此例需要上層4輸出、下一層32/64輸出；即便庫能驅動64，建構也會截到32。後續嚴格改善搜尋難以從高成本的另一個zero-skew拓樸拆換成整組ring forest。

可增加少量按層分支數政策（例如上層4/8、葉層使用可用fanout），或用有界cluster代表點與其到所有葉子的相等距離辨認群組，再由SRC附近的一個branch連接。以有效fanout算出的必要buffer數可用來評估分组；避免所有層都貪取最大arity。對高fanout可採獨立工作上限而非完全固定32。

本issue不要求一般問題保證最優，也不要求移除工作上限；這是已知最優的小型規律布局，可作為新的bounded construction政策驗收案例。

## 重現

```sh
python3 'issues/opened/026 分層ring布局保留八十二至一百二十九buffer成本/repro/verify.py' build/cbi
```

checker先驗證witness，再驗證solver合法性與stdout，Score≤5才通過。輸出放`tmp/tester/026/`；未修改程式，未執行grader。
