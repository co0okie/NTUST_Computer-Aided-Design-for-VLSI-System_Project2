# 報告

- `main.tex`：完整正文與三張 TikZ 圖，12pt 標楷體／Times New Roman。
- `main.pdf`：三頁 A4 提交版本，署名李品翰／M11507401。
- `build.sh`：雙次編譯與頁數／字型缺字／版面檢查。
- `data/`：同測資三階段實驗紀錄、選出的同次執行數據及原理示例驗算。

在 repo 根目錄執行：

```sh
bash report/build.sh
```

需要 XeLaTeX、標楷體、Times New Roman、TeX Gyre Termes Math，以及
`pdfinfo`。中文圖表與正文均維持 12pt；數學使用 Times 相容的 Termes Math。
腳本編譯兩次，檢查三頁上限、溢出、缺字及交叉引用，將 PDF 寫入
`report/main.pdf`，中間檔留在已忽略的 `tmp/coder/report-build/`。

主表使用正式程式 `2c35ad8` 的 15 次 hybrid 執行，Ubuntu 24.04／GCC13、
2 CPU／8 GiB、無網路容器、每次 25 秒。每個案例以最終 Score、seed
依序排序，取五次中的第三次；三階段取自該次執行，沒有分別取指標中位數。
首次合法解與 SA 初始解來自程式已完整驗證的 checkpoint；SA 初始解為
合法樹建構與局部搜尋後進入 SA 的解。buffer 成本由
`Score - w_skew × skew` 還原，最終成本另與獨立整數驗證器的結果核對。
這三例用於說明改善來源，並非未經挑選的整體效能測試。

表中「作業範例」來自 `example/input.cbi`；「隨機生成例」來自
`benchmarks/sa_lp/inputs/input024.cbi`，由 `tools/benchmark.py` 以生成 seed
20261013 產生的 30 例測資中選取，含 18 個 sinks 與兩種 buffer 型別。
`024` 是檔案編號。測資生成 seed 與本次五個 SA seeds 分別控制輸入生成
及搜尋亂數。「子樹交換回歸例」為 issue 019 的重現測資，完整路徑記於
`data/protocol.json`。

相關消融與參數研究位於 `benchmarks/ablation/`、`benchmarks/tuning/`。
時間受環境影響，固定時間重跑不保證相同搜尋路徑或最終分數。

消融表使用 `benchmarks/ablation/runs.csv` 的配對紀錄，由固定實驗版本
`0772bca` 每次停用一個組件。這批測試早於正式版清理；表中的每項均與
同版本、同輸入、同 seed 的完整基準比較。`data/ablation-summary.json`
保存選出的案例、重新計算的比較結果、版本及來源雜湊。

- 基本局部搜尋與空位受限建樹：79 個輸入，1～1024 sinks；前者停用
  `src/search/local.cpp` 的基本操作與排程，聯合操作及大樹合併保留。
- SA＋LP 階段、LP 座標求解：44 個輸入，1～21 sinks，三個 seeds、
  至多 100 次 SA 操作及 25 秒上限；每例比較三次 Score 的中位數。
  停用 LP 仍沿用既有／估計座標並保留修復與驗證；停用整個 SA＋LP
  階段則不執行 SA 操作。
- 大樹 buffer 合併：三個 600-sink、三個 1000-sink 輸入，來自
  `benchmarks/ablation-holdout/inputs/large*.cbi`。所有六例停用後 skew
  不變、buffer 成本增加；報告的數值例為 `large005.cbi`。

大型測試另外有 256～2048 sinks 的短時限初始解壓力案例，但不能替代
大規模 SA＋LP 的固定 25 秒評估。報告目前將大型測資的收益限於建構／
合併組件，並把大規模 SA／LP 消融列為後續實驗。

「LP 目標的計算取捨」採用 `benchmarks/tuning/` 的獨立參數研究：
固定迭代在八個訓練案例比較；固定時間在四個保留案例比較。報告以
參數取捨說明兩種座標解擇優的額外成本。移除重複收尾與略過相同
型別重試的清理驗證保留於 benchmarks，不列為方法貢獻的消融證據。
