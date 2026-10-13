# 開發與驗證

助教使用的編譯與執行介面見 [README](../README.md)。本文件保留測試、
搜尋參數與實驗重現方式；協作規則見 [AGENTS.md](../AGENTS.md)。

## 測試建置

預設 CMake 建置僅產生 solver。開發時另外建立測試目錄：

```sh
cmake -S . -B build-tests -DBUILD_TESTING=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

CTest 包含 search、annealing、solver 與 CLI 四組；Python 測試需要
Python 3。`tests/test_solver.py` 與 `tools/benchmark.py` 獨立檢查輸出
樹的座標、拓樸、fanout、總線長、到達時間與 Score。

預設啟用 unity build，將核心 source files 合成一個 translation unit，
減少重複解析 headers。需要逐檔編譯時使用 `-DCBI_UNITY_BUILD=OFF`。
GNU／Clang 編譯使用 `-pipe`。測試與 sanitizer 選項各由 CMake 控制。

```sh
cmake -S . -B build-sanitized -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DCBI_SANITIZERS=ON
cmake --build build-sanitized
ctest --test-dir build-sanitized --output-on-failure
```

若執行環境禁止 ptrace，可在執行測試時設定 `ASAN_OPTIONS=detect_leaks=0`，
停用該環境無法運作的 LeakSanitizer；隨附的 HiGHS 函式庫未加入 sanitizer
instrumentation。

## 策略與搜尋參數

| `--strategy` | 流程 |
| --- | --- |
| `current` | 合法樹建構與局部搜尋。 |
| `sa_lp` | 取得合法初始解後執行 SA＋LP；建構需要時會使用完整建構流程作為 fallback。 |
| `hybrid`（預設） | 保存快速合法解，與完整建構／局部搜尋結果比較後，再執行 SA＋LP。 |

以相同 seed 與迭代上限比較策略，例如：

```sh
mkdir -p tmp/coder/search
./build/cbi example/input.cbi tmp/coder/search/output001.cbi \
  --strategy hybrid --seed 7 --iterations 100 --time 25 \
  --stats tmp/coder/search/search.json
```

`--iterations 100` 設定至多 100 次 SA 操作；到達 Score 下界或時間用完
時可提早結束。預設 seed 為 20261013。固定 seed、迭代數及相同 binary
可用於差分比較；限時搜尋仍受機器速度與負載影響。

| 參數 | 控制內容 |
| --- | --- |
| `--temperature`、`--cooling-ratio` | 起始溫度與降溫比例；溫度 0 使用分數差校準。 |
| `--move-weights` | 七種 SA 操作的抽樣權重，以逗號分隔。 |
| `--locality`、`--locality-k`、`--subtree-sinks` | 鄰近候選抽樣與子樹大小。 |
| `--current-fraction` | `hybrid` 初始解階段可用的總時間比例。 |
| `--lp-time`、`--max-lp-nodes` | LP 時間設定與模型節點上限。 |
| `--repair-trials`、`--structural-repair 0\|1` | 整數鄰域修復次數與拓樸修復開關。 |
| `--wire-tiebreak 0\|1` | 是否在最小 skew 後再最小化總線長。 |

七種權重依序為 sink 改接、sink parent 交換、buffer 插入、buffer 刪除、
相對象限變更、型別變更、子樹改接。完整參數解析見 `src/main.cpp`；
預設值集中在 `src/solver/strategy.hpp`、`src/search/annealing.hpp` 與
`src/search/placement.hpp`。

`--stats FILE` 保存初始解、改善過程的 checkpoint、各操作次數與
LP／修復耗時。`lp_calls` 計數的是座標求解流程的一次嘗試，包含其可選
的第二次線長求解；比較效能時需保持這個計數單位一致。

## 文件與資料

- [合法樹建構與局部搜尋](construction.md)：建構候選、relay 與 Score 下界。
- [SA＋LP 實作](sa-lp.md)：狀態 ownership、LP、整數修復與接受規則。
- [環境驗證](grader-environment.md)：Ubuntu 容器、編譯與執行紀錄。
- [研究報告](../report/main.pdf)與[報告重現說明](../report/README.md)：圖表與實驗來源。
- `benchmarks/`：SA／LP 比較、消融及參數實驗的測資與紀錄。
- `issues/`：問題、留言、重現測資與 tester 驗證紀錄。

實驗資料保留測試版本、seed、時間／迭代預算與獨立合法性驗證。
舊版 README 中的歷史指標可由 `git show 2c35ad8:README.md` 查閱；
目前實驗依各 benchmark 的 protocol 與原始紀錄重現。

## 交付檔案

source archive 需要保留 `CMakeLists.txt` 與完整 `src/`，包含 HiGHS
headers、靜態函式庫與授權檔。產生的 build、輸入／輸出暫存及實驗工作
目錄由 `.gitignore` 排除。提交命名及格式依
[Project2 Requirement.pdf](<Project2 Requirement.pdf>)；報告 source、PDF
與圖表資料保存在 `report/`。

交付檔名為 `M11507401.zip` 與 `M11507401.pdf`；zip 中使用
`M11507401/CMakeLists.txt`、`M11507401/README.md` 與 `M11507401/src/`。
報告由 `report/main.pdf` 以交付檔名另行提供。README 的指令以解壓縮後
的 `M11507401/` 為工作目錄，不依賴 repository 中的 `example/` 或 `report/`。
繳交副本放在已忽略的 `tmp/`；ZIP 僅包含學號目錄下的
`CMakeLists.txt`、`README.md` 與完整 `src/`，報告 PDF 放在 ZIP 外。

已目讀 grader 的 `grade.sh` 與 `executor.py`：解壓縮到
`/home/grader/M11507401/` 後，由 grader 提供 `inputs/inputXXX.cbi` 並建立
`results/`；用預設 CMake 建置，再以 `build/cbi INPUT OUTPUT` 執行。
不需從 README 取得旗標、執行 setup script 或讀取 stdin。
