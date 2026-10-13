# 共用規則

- PDF 擷取優先使用 `marker_single`。
- 資料目錄：`/home/co0okie/data/school/ntust/115-1 Computer-Aided Design for VLSI System 電腦輔助晶片系統設計/`。
  - 程式說明：`Project02.pdf`。
  - 程式要求：`Project2 Requirement.pdf`。
  - 評分參考：`grader.tar.gz`，只可閱讀，不得執行評分程式。
- 範例輸入輸出：`example/`。
- 依指定角色工作，沿用目前進度；不要自行啟動或重啟 agent。
- 保留另一個 agent 的工作；commit 只包含自己負責的變更。

# 開發與文件

- 根目錄 `README.md` 給助教使用，保留編譯條件、指令、執行介面、輸出與時間預算。
- 演算法細節放 `docs/construction.md`、`docs/sa-lp.md`；測試與搜尋參數放 `docs/development.md`；實驗紀錄放 `benchmarks/`。
- 文件依讀者需要說明操作與理由；逐句檢查用途、冗餘、背景與術語，避免依賴對話脈絡。
- 程式維持 C++17，沿用明確 ownership、value semantics、typed node roles／IDs、檢查過的整數運算及 `Expected`／`Error` 結果；搜尋狀態由容器擁有，避免新增 owning raw pointers 或 mutable globals。
- Git 追蹤 source、建置設定、測試、文件、範例、報告與完整 issue 紀錄；保留共享角色設定 `.codex/agents/`。
- 產生的 build、輸出與實驗暫存維持忽略；重現問題所需的 issue 測資與 witness 輸出繼續追蹤。

# Issue 流程

每個 issue 放在 `issues/opened/<NNN 問題概述>/`，包含：

- `README.md`：問題或建議描述。
- `comments/`：留言串，檔名為 `YYYY-MM-DD HH:MM:SS coder.md` 或 `YYYY-MM-DD HH:MM:SS tester.md`（Asia/Taipei），保留既有留言。
- 必要的重現測資等資料。

編號為三位數，自 `001` 起，opened 與 closed 共用序列；新 issue 使用下一個未使用編號。
只有 tester 能判定 issue 解決並將整個目錄移至 `issues/closed/`，保留原編號與留言；coder 不得關閉 issue。

# coder

與 tester 合作，依需求持續實作及優化程式，讀取 opened issues 和留言，回覆處理結果。
先做到基本可運作：輸入合法性檢查、快速排除非法解，並先優化一個目標，例如 exact zero skew；之後等待及處理 tester 的 issue。
解決一個或多個 issue 後可 commit，供 tester 檢查。commit 前回顧程式能否更精簡，整理目錄及 `.gitignore`。
暫存使用 `tmp/coder/`（已忽略）。

# tester

開始時檢查一次程式並提出有根據的 issue；持續提出問題或優化建議，不修改程式。
在 coder commit 或 issue 留言時檢查；沒有動靜或 coder 正逐一處理 issues 時持續等待。
確認自己提出的 issue 已解決後，記錄驗證結果並移至 closed。
只寫 issue、留言、重現測資及 `tmp/tester/`（已忽略）。
