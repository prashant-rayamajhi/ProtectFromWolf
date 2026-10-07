# 処理の流れ

[READMEへ戻る](../README.md)

掲載中のC++コードを読むためのフローチャートです。細かな補助処理は省略し、条件分岐と各部品の役割をまとめています。未掲載のBlueprintを含むゲーム全体の動作保証や、実機検証結果を表す図ではありません。

## 1. 人型と人狼の切り替え

体格を変えるだけでは足元の高さがずれるため、Capsuleの寸法変更とMeshの位置補正を分けて処理しています。

```mermaid
flowchart TD
    A["EnterForm：変身の適用要求"] --> B{"プレイヤー・メッシュ・AnimBP・コンボが有効か"}
    B -->|いいえ| X["失敗を返す"]
    B -->|はい| C{"Mesh・Capsule・Movementがあるか"}
    C -->|いいえ| X
    C -->|はい| D{"人型の寸法を保存済みか"}
    D -->|いいえ| E["Capsule寸法とMeshの相対Transformを保存"]
    D -->|はい| F["狼男のMesh BoundsからCapsule寸法を計算"]
    E --> F
    F --> G["Half Heightの差をActor位置へ反映し、Capsuleサイズを変更"]
    G --> H["狼男MeshとAnimBPへ切り替え、ポーズとBoundsを更新"]
    H --> I{"足ボーンがあるか"}
    I -->|はい| J["左右の足のうち低い位置を接地基準にする"]
    I -->|いいえ| K["Mesh Boundsの下端を接地基準にする"]
    J --> L["接地基準とCapsule底面の差でMesh位置を補正"]
    K --> L
    L --> M["狼男の移動速度・ジャンプ力を適用して成功を返す"]
    M -.->|呼び出し側から解除要求| N["ExitForm：復元に必要な参照を確認"]
    N --> O{"復元に必要な参照が有効か"}
    O -->|いいえ| P["変更せず終了"]
    O -->|はい| Q["保存したCapsule寸法とMesh Transformへ戻す"]
    Q --> R["人型Meshと指定されたAnimBPへ戻す"]
    R --> S["呼び出し側から渡された移動速度・ジャンプ力へ戻す"]
```

人型の寸法は最初の変身時だけ保存します。解除時に狼男の値を基準に逆算し続けず、保存した値へ戻すことで補正の累積を避けています。人型のMesh・AnimBP・移動性能は解除時の引数で受け取ります。変身の受付、残り時間、カメラ演出はこの部品の外側で管理されるため、図の対象外です。

参照：[WerewolfFormComponent.cpp](../Source/ProtectFromWolf/Private/Player/WerewolfFormComponent.cpp) の `EnterForm`・`ExitForm`。

## 2. 敵が判断に使う情報の更新

この図はBehavior Treeの配置図ではなく、Serviceが判断材料を更新する流れです。更新間隔は0.2秒です。

```mermaid
flowchart TD
    A["EnemyContextを定期更新"] --> B{"敵本体とBlackboardがあるか"}
    B -->|いいえ| Z["今回の更新を終了"]
    B -->|はい| C["対象参照を取得し、必要ならプレイヤーPawn参照を補う"]
    C --> D["射撃中の弾切れを処理し、停止・死亡・行動制限を記録"]
    D --> E{"演出停止・死亡・Teleport・ノックバック中か"}
    E -->|はい| Z
    E -->|いいえ| F{"対象が存在するか"}
    F -->|いいえ| G["視認と攻撃範囲をfalseにして終了"]
    F -->|はい| H["CanObserveTargetで直接視認を確認"]
    H --> I["Controllerの視覚記憶と戦闘記憶を更新"]
    I --> J["装備・認識・現在のカバー状態に応じて遮蔽物行動を更新"]
    J --> K{"直接視認できているか"}
    K -->|はい| L["DecisionComponentで攻撃スタイルを再評価"]
    K -->|いいえ| M["攻撃スタイルの再評価を行わない"]
    L --> N["現在の武器で距離・近接の間合いを判定"]
    M --> N
    N --> O["距離・戦闘認識・攻撃範囲をBlackboardへ書き戻す"]
    O --> P["Behavior Treeの条件判定と各Taskが参照する"]
```

対象の参照を持つことと、直接視認できることは別です。また、現行のBlackboardの `CanSeeTarget` は名前に反して、直接視認だけでなくControllerの戦闘認識やカバー使用状態も含む値です。攻撃範囲の更新では直接視認も確認します。ここを分けて読むと、「相手を見失ったが警戒は続いている」状態を追いやすくなります。

遮蔽物行動は単純な移動先の設定だけではなく、カバーへの移動、待機、射撃位置への移動、射撃可能時間、リロードなどを別の部品で扱います。必ず遮蔽物を選ぶという意味ではありません。

参照：[BTService_EnemyContext.cpp](../Source/ProtectFromWolf/Private/AI/BehaviorTree/Services/BTService_EnemyContext.cpp)、[EnemyCoverComponent.cpp](../Source/ProtectFromWolf/Private/Enemy/Components/EnemyCoverComponent.cpp)、[EnemyBehaviorTasks.cpp](../Source/ProtectFromWolf/Private/AI/BehaviorTree/Tasks/EnemyBehaviorTasks.cpp)。

## 3. ボスが次の攻撃を選ぶ流れ

攻撃の「スタイル変更」と「具体的な技の選択」は別の処理です。以下は `ExecuteAttack` から技を選ぶ部分です。

```mermaid
flowchart TD
    A["ExecuteAttack：攻撃要求"] --> B{"死亡・別行動・装備切替・攻撃不可のカバー状態か"}
    B -->|はい| X["攻撃を開始せずfalseを返す"]
    B -->|いいえ| C{"対象とControllerが有効で、対象を直接視認できるか"}
    C -->|いいえ| X
    C -->|はい| D{"通常敵か"}
    D -->|はい| E["PerformAttackを呼び、攻撃開始の成否を返す"]
    D -->|いいえ| F{"次のボス攻撃が可能な時刻か"}
    F -->|いいえ| X
    F -->|はい| G["距離・装備・残弾・観測した行動傾向を取得"]
    G --> H["通常攻撃・地面攻撃・レーザー・離脱・接近を個別に評価"]
    H --> I["実行条件と再使用待ちを確認し、直前と同じ特殊技は減点"]
    I --> J{"実行可能な候補が残ったか"}
    J -->|いいえ| X
    J -->|はい| K["選ばれた行動をCommitBossActionで実行"]
    K --> L{"攻撃またはTeleportが開始されたか"}
    L -->|いいえ| X
    L -->|はい| M["使用した行動と時刻を戦闘記憶へ記録"]
    M --> N["直前の技と次の攻撃可能時刻を更新してtrueを返す"]
```

例えば、通常攻撃は残弾や近接の間合いを確認し、レーザーは距離と静止傾向などから評価します。離脱・接近のTeleportはラストボス用の条件があります。候補が残らない場合や技が開始できなかった場合には、成功したことにせず戻ります。

`CommitBossAction` には跳躍と連続射撃の分岐もありますが、現在の `ExecuteAttack` の候補一覧には登録されていないため、この図には含めていません。行動傾向の記録を使う仕組みであり、機械学習で訓練したものではありません。

参照：[EnemyDecisionComponent.cpp](../Source/ProtectFromWolf/Private/Enemy/Components/EnemyDecisionComponent.cpp) の `ExecuteAttack`・`CommitBossAction`・`UpdateBossStyle`、[EnemyCombatMemoryComponent.cpp](../Source/ProtectFromWolf/Private/Enemy/Components/EnemyCombatMemoryComponent.cpp)。
