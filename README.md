# Clock Buffer Insertion

李品翰／M11507401

## 編譯

在解壓縮後的 `M11507401/` 目錄執行，環境為 Linux x86-64、GCC（C++17）、
CMake 3.16 以上與 Make。HiGHS 已包含於 `src/vendor/highs/`。

```sh
cmake -S . -B build
cmake --build build
```

## 執行

兩個參數依序為輸入與輸出檔案；grader 會提供 `inputs/` 並建立 `results/`。
手動執行時先建立輸出目錄：

```sh
mkdir -p results
./build/cbi inputs/input001.cbi results/output001.cbi
```

預設搜尋時間預算為 55 秒，可用 `--time 25` 改為 25 秒。
研究報告另以 `M11507401.pdf` 繳交。
