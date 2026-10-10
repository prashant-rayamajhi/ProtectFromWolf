# PROTECT FROM WOLF

チーム制作作品のうち、変身処理・敵用武器・戦闘処理・敵AIを紹介するC++リポジトリです。ゲーム本体はReleasesで別途配布しています。

## 作品概要

最新のWindows配布版は**2026年10月10日更新**です。味方の交戦情報の共有、戦闘中の巡回への逆戻り、ボスの待機姿勢と銃の収納タイミングを見直しました。[最新版のダウンロードと更新内容](https://github.com/prashant-rayamajhi/ProtectFromWolf/releases/tag/v2026.10.10-enemy-team)をご確認ください。

銃を使う人型のFPS操作と、人狼へ変身した後のTPS近接戦闘を切り替えて戦うアクションゲームです。

- 制作期間：約3か月（2025年11月20日～2026年2月13日）
- 制作体制：チーム制作（プログラマー3名）
- 使用技術：Unreal Engine 5／C++（現在の改修版はUE 5.7.4）
- [プレイ動画](https://drive.google.com/file/d/1k1E9EClqOoNjFCqNBbwQfi-2LEg7HgVN/view?usp=drive_link)
- [作品資料](https://drive.google.com/drive/folders/1lv9nxbNX8tpTkDl2XpudB7sLtmPInEcj)

制作期間終了後の不具合修正・責務分割・コメント整理も含みます。制作当初の実装と、その後の改修は区別して説明します。

## 掲載している担当範囲

- プレイヤーキャラクターの一部：人型と人狼の変身・解除処理
- 通常敵・中間ボス・ラストボスのキャラクター制御
- 敵専用の銃・弾・近接武器と敵側の戦闘処理
- 敵AIの知覚、捜索、状態管理、攻撃判断、移動、遮蔽物利用、味方との連携

プレイヤーの射撃処理は一部担当です。共有ファイル全体を単独の成果として扱わず、担当行を確認できていない部分は掲載していません。

| 分野 | 主なソース |
| --- | --- |
| 変身と解除 | [WerewolfFormComponent.cpp](Source/ProtectFromWolf/Private/Player/WerewolfFormComponent.cpp) |
| 敵用武器 | [Weapons](Source/ProtectFromWolf/Private/Weapons) |
| 戦闘・ボス技 | [EnemyCharaCombat.cpp](Source/ProtectFromWolf/Private/Enemy/EnemyCharaCombat.cpp)、[EnemyCharaBoss.cpp](Source/ProtectFromWolf/Private/Enemy/EnemyCharaBoss.cpp) |
| 敵用攻撃部品 | [Combat](Source/ProtectFromWolf/Private/Combat) |
| 知覚・行動遷移 | [AI](Source/ProtectFromWolf/Private/AI) |
| 状態・体力・遮蔽物・連携 | [Enemy](Source/ProtectFromWolf/Private/Enemy) |

対応するヘッダーは`Public`に掲載しています。[掲載範囲の詳細](Source/README.md)も参照してください。

## フローチャート

実装の条件分岐と役割を、[3つのフローチャート](docs/FLOWCHART.md)にまとめています。

- [人型と人狼の切り替え](docs/FLOWCHART.md#1-人型と人狼の切り替え)
- [敵が判断に使う情報の更新](docs/FLOWCHART.md#2-敵が判断に使う情報の更新)
- [ボスが次の攻撃を選ぶ流れ](docs/FLOWCHART.md#3-ボスが次の攻撃を選ぶ流れ)

```mermaid
flowchart LR
    A["変身要求"] --> B["体格と接地位置を補正"] --> C["人狼の能力を適用"]
    C --> D["解除要求"] --> E["人型の基準値へ復元"]
```

## 変身処理の工夫

体格の違う人型と人狼を切り替える際、Capsuleの中心を固定して高さだけを変えると底面が上下し、浮きや床埋まりにつながります。

初回の人型の寸法・Mesh Transform・移動性能を保存し、変身時にはMesh Boundsから必要なCapsule寸法を計算します。Half Heightの差をActor位置へ反映して底面を維持し、左右の足Boneを優先して足元を補正します。解除時には保存した基準値へ戻し、変身の繰り返しで補正が累積しないようにしています。

## 敵AIと戦闘処理

ControllerとAI Perceptionが視覚・聴覚による発見や最後に確認した位置を扱い、Behavior TreeのServiceが戦況をBlackboardへ反映します。Taskは追跡・捜索・攻撃・再配置・巡回を実行します。

ボスは距離、残弾、被弾状況、味方の配置などから近接・銃・レーザーの候補を採点します。候補ごとの評価と最終選択を分け、固定した条件文の順序だけに依存しない構造にしています。再使用までの時間や直前の行動も確認します。これは機械学習で訓練した仕組みではありません。

敵用武器、体力、状態、遮蔽物探索を部品に分け、攻撃の開始条件と中断・終了後の復帰を追いやすくしています。近接攻撃では距離・方向・高低差を確認し、予約済みの処理が中断後に残らないようタイマーを解除します。

## 確認方法と残る課題

Blackboard、状態、攻撃候補の点数、移動先などを確認し、画面上の挙動と記録を照合して切り分けています。ビルドや自動テストの成功だけで、すべての戦闘が正しいとは判断していません。敵の密集など、未解決の条件も残っています。

詳細な実装説明は[主要コード解説](docs/CODE_WALKTHROUGH.md)を参照してください。

## ゲームの入手と起動

[最新Windows版](https://github.com/prashant-rayamajhi/ProtectFromWolf/releases/latest)のAssetsから **`ProtectFromWolf.zip`** をダウンロードし、全体を展開して **`Windows/ProtectFromWolf.exe`** を起動します。EXEだけを取り出さず、付属フォルダも一緒に保持してください。配布ZIPには実行に必要なファイルのみを収録し、元プロジェクトやC++ソースコードは含めていません。

### 2026年10月10日の更新

- 味方の交戦報告を取り逃した敵も、同じ戦闘空間の視認・物音の記録から警戒と捜索を開始するよう修正。
- 戦闘情報が残っている間は巡回に戻らず、相手を見失った場合は最後に確認された位置を捜索。
- ボスの休息モーションを戦闘用の構えへ切り替え、銃をしまう動作と非表示のタイミングを調整。
- 射線のない敵が味方の移動待ちで長く停止する処理を見直し、別の射撃位置を探すよう改善。

最新版は改修中のプロジェクトからUE 5.7.4のShipping構成で作成しています。掲載ソースは担当範囲の抜粋であり、この更新では実行データとREADMEを更新しています。掲載コード全体を配布版と同期したという意味ではありません。

**確認範囲：** エディターで実アセットを使った個別試験8条件と自動テスト38件を確認しました。自動テストの失敗は0件、6件に簡易テスト用メッシュ未設定の警告があります。配布版のパッケージ作成は成功していますが、配布版でタイトルからゲームクリアまでの通しプレイは未実施です。


| 操作 | キーボード・マウス | ゲームパッド（Xbox表記） |
| --- | --- | --- |
| 移動 | W / A / S / D | 左スティック |
| 視点 | マウス | 右スティック |
| 攻撃 | 左クリック | RT |
| 照準 | 右クリック | LT |
| リロード | R | X |
| ジャンプ | Space | A |
| 変身 | 左Shift | RB |
| ジャスト回避 | 左Alt | B |

変身にはゲージ、ジャスト回避には適切なタイミングが必要です。

