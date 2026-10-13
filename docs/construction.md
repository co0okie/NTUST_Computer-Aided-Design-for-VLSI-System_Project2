# 合法樹建構與局部搜尋

本文件整理初始解的建構方式及局部搜尋；SA 的拓樸操作、LP 座標求解與
整數修復見 [SA＋LP 實作](sa-lp.md)。

## 從完整候選中選擇初始解

先檢查 SRC 直連所有 sinks 是否合法。需要 buffer 時，依 pin 分布、
型別的 fanout 與輸出線長上限嘗試多種建構。每個候選都包含 parent-child
關係與整數座標，通過完整驗證後才參與 Score 比較。

| 建構方式 | 適用情況 | 做法 |
| --- | --- | --- |
| 二元空間樹 | 一般分布、可用分支型別 | 沿座標跨度較大的方向排序，按數量分成兩組，遞迴合併子樹。 |
| 多輸出空間樹 | 型別可驅動多個 children | 一次合併多組，利用較大的 fanout 減少分支 buffer 數。 |
| 分層群組 | sinks 有群聚 | 以 Manhattan distance 分群，先建群內子樹，再連接各 root。 |
| relay 鏈 | 長線或單輸出型別 | 分段接續；對不同型別組合估計成本，再嘗試實際放置。 |
| forest 搜尋 | 少量 sinks 的分組難以接至 SRC | 保留多種子樹合併候選，讓 SRC 可驅動多個 root。 |
| 空位受限建樹 | 可放 buffer 的格點少 | 以內部空位枚舉 buffer 的 parent-child 關係，再分配 sinks。 |

分支位置會採樣 child roots 的中點及到達時間平衡位置。對內部路徑較長
的子樹，嘗試縮短新分支 buffer 到其 root 的連線。每個 driver 的限制
同時包含 fanout 與所有輸出邊的長度總和；relay 可將超出上限的直接
連接拆段。

relay 的估價使用 coverage-cost DP。令 `r` 為尚需接續的長度，型別 `t`
的線長上限與成本為 `length_t`、`cost_t`，則：

```text
C(0) = 0
C(r) = min_t { cost_t + C(max(0, r - length_t)) }
```

若所有型別線長有公因數 `g`，可將容量除以 `g`，並以 `ceil(r/g)` 表示
待接續長度，減少 DP 的狀態數。實際放置仍使用原始單位，檢查格點是否
位於晶片內、是否占用，以及完整樹的 driving limits。被占用的理想位置
可能需要改用其他型別或位置。

晶片很大而 pins 集中時，可另以 pin 附近的較小座標範圍嘗試建構，再
平移回原座標。平移保持 Manhattan distance，但候選仍須依原晶片邊界
與輸入限制驗證。

## 改善完整合法樹

基本局部搜尋嘗試 buffer 刪除／合併、buffer 移位、sink 改接、parent
交換及繞路 relay 插入。每次先依實際 fanout 與總線長重選最便宜的合法
型別，驗證完整樹，再以嚴格下降的 Score 接受候選。改善後重新排入
相關操作，使新的座標或拓樸有機會觸發下一次改善。

聯合操作一次改變多個部分，再評估完成後的樹。例如刪除分支 buffer
並移動其兩個 buffer children，或同時改變 hub 的 children 與座標。
這些操作處理單次移位或改接難以完成的改變。

大樹使用單獨的 buffer 合併。若 parent 直連 grandchild 的 Manhattan
distance 等於原兩條邊長總和，移除中間 buffer 可保持所有 sink 到達
時間。只有 parent 的新型別與輸出總長合法，且總成本下降時才採用。

各建構與改善都有嘗試次數、元件數及時間限制；搜尋期間保留已完成的
最佳合法樹。具體上限集中在 `src/construction/`、`src/search/local.cpp`
與 `src/solver/legacy.cpp`，以避免文件中的數字與實作分離。

## Score 下界與提前停止

整數座標唯一使每條邊至少長一個單位。因此型別的有效 fanout 為
`min(fanout, length)`。令 `N` 為 sinks 數、`S` 為 SRC 的有效 fanout、
`F` 為 buffer library 中最大的有效 fanout，當 `F > 1` 時，buffer
數量至少為：

```text
ceil(max(0, N - S) / (F - 1))
```

將此數量乘以最低型別成本可得成本下界。若 SRC 直連超過輸出線長上限，
buffer 數量下界至少為一。Manhattan distance 的奇偶性由端點決定，
因此 sinks 的 `(x+y) mod 2` 不全相同時，skew 至少為一，Score 下界
還須加上輸入的 skew 權重。

完整合法樹達到此下界便可提前停止。下界計算使用檢查過的整數運算；
無法安全計算時繼續原搜尋。建構候選耗盡則回傳建構錯誤。

## 驗證紀錄

[消融實驗](../benchmarks/ablation/README.md) 保存局部搜尋、空位受限建樹
及大樹合併的配對比較；[參數研究](../benchmarks/tuning/README.md) 保存
LP 目標與重試的實驗。較早版本的詳細試探預算、差分測試及 issue 驗收
紀錄可由 `git show 2c35ad8:README.md` 查閱。
