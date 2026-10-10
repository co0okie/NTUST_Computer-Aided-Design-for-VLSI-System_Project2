# 密集 forest 僅 SRC 直連 buffer，漏掉階層合法樹

檢查版本：`c6c8894`、`7d87ec4`。023已修正SRC直接驅動三buffer的dense forest；本例SRC只有一個輸出，合法樹必須buffer→buffer，有別於023直接forest模型。

## 4×4滿佔位、21 sinks

SRC=(4,3)，fanout=1、length=1；單一buffer fanout=8、length=27、cost=1，w=1。

所有25個整數座標，除SRC與合法witness的三個buffer位置外，其餘21個都是sinks，恰好只有三個可用interior位置：

- B1=(3,3)，SRC→B1，距離1。
- B2=(1,1)、B3=(1,3)，B1→{B2,B3,6個sinks}；fanout8、outgoing total15。
- B2→8個sinks，outgoing total13。
- B3→7個sinks，outgoing total27。

完整witness通過独立checker：T_max=9、T_min=2、cost=3、Score=10。原序與反序兩份測資都被solver拒絕，exit1：`no legal tree constructed: library cannot drive all sinks from one hub`。

本issue驗收為找到任一合法樹，不要求Score10或全域最優。

## 缺口及有界方向

dense_forest_search只枚舉SRC直接連接buffer的subset，再把sink分配給SRC或這些buffers。SRC fanout1最多選一個buffer；其fanout8不足以接21 sinks，容量檢查會跳過，但三buffer的階層树完全合法。

multiway固定高arity會需要過多buffer位置；layered 4/8群也至少要五個buffer，超過三個free slots。小forest仍受12-sink截止限制。此例只有三個可用buffer座標，並不需要枚舉大晶片或所有sink拓樸。

建議dense fallback先對至多8個free slots建立有界的buffer骨架（允許SRC→buffer與buffer→buffer），預留骨架fanout/總length，再延用sink assignment。用樹連通性、祖先迴圈、有效容量与remaining interior count剪枝，所有骨架與sink assignment共享固定work budget。這可補回src fanout小、少數buffer形成階層的密集布局。

## 重現

```sh
python3 'issues/opened/030 密集forest僅SRC直連buffer漏掉階層合法樹/repro/verify.py' build/cbi
```

checker驗證witness與solver完整限制及stdout，兩份solver合法即可通過。輸出在`tmp/tester/030/`；未修改程式，未執行grader。
