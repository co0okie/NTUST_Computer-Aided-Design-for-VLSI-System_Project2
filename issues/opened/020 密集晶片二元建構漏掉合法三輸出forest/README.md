# 020 密集晶片二元建構漏掉合法三輸出 forest

正確性問題：commit `d49b3f8` 對下列兩個已知合法輸入均 exit 1，錯誤為 `no legal tree constructed: library cannot drive all sinks from one hub`。附帶完整 witness，均通過獨立輸出驗證。不是要求未知輸入全域最佳化，而是已有明確合法解時不應拒絕。

## 反例 1：6 sinks，只有兩個 buffer 座標

`repro/input001.cbi`：4×4，SRC=(2,2)，source fanout=2、總輸出線長=2；T0=(fanout 3, length 4, cost 1)，w_skew=100。sink 座標為 (1,1),(1,3),(2,1),(3,1),(3,3),(2,3)。

合法的 interior integer positions 只有 1..3 × 1..3 共 9 個，其中 SRC/sinks 已佔 7 個，只剩 (1,2)、(3,2)。`witness001.cbi` 恰好在兩處各放一個三輸出 buffer，SRC:{B1 B2}，每個 buffer 各接三個 sinks。

- SRC 出線總長 1+1=2，fanout 2。
- B1、B2 出線各為 1+1+2=4，fanout 3。
- 全部座標唯一，buffer 嚴格在晶片內，連通且無環，無空 buffer。
- sink 到達時間 [2,2,3,2,2,3]；(T_max,T_min,cost,Score)=(3,2,2,102)。

二元 forest 不可能在這個位置容量下成立：6 leaves、SRC 最多 2 children，其餘分支最多 2 children，至少需要 6−2=4 個 buffers，但僅有兩個合法空位。現有 `Builder::build` 二元架構需要更多位置；`small_forest` 雖接受 ≤8 sinks，仍只合併兩個 roots，因此也不能建立上述三輸出 forest。事後 `improve_tree`/`contract_large_tree` 無法拯救尚未建構成功的樹。

## 反例 2：9 sinks，超過現有 fallback 的 sink 上限

`input002.cbi`：5×5，SRC=(1,1)，source fanout=3、總長=4；T0=(3,6,1)。`witness002.cbi` 用三個三輸出 buffers 各接三個 sinks，metrics=(4,2,3,203)。SRC 線長4，三個 buffers 出線總長分別5/5/6。

16 個 interior 位置中 SRC/sinks 佔10個，尚有6個空位；二元 forest 至少需要9−3=6個 buffers，但常規單一SRC-root二元建構需8個且無法配置；目前 fallback 直接因9 sinks略過。此例防止只提高 merge arity、卻保留8-sink gate而仍漏掉同類合法輸入。

## 建議與驗收

可考慮有界多輸出 forest construction/fallback：merge arity 在可用 buffer fanout 內，依有限的 sinks/roots、候選位置、beam/trial budget 控制工作量；先完整 validate 再接受。也可採其他合法建構方法，不指定實作。注意只提高二元搜尋 budget 或只在完成後做 bypass，不能解決反例1的座標容量。

驗收兩份輸入均成功輸出完整合法樹；不要求特定 Score 或拓樸。保留舊有可行案例及工作上限，不假稱有界 fallback 能證明一般無解。witness 只使用正常資料與 T0，没有額外規則。

## 重現

`python3 issues/opened/"020 密集晶片二元建構漏掉合法三輸出forest"/repro/verify.py <solver binary>`

此工具獨立驗證兩個 witness，執行 solver 並報告拒絕或合法 metrics；拒絕時 exit 1。目前 d49b3f8 兩例均拒絕。驗證不執行或匯入 grader。
