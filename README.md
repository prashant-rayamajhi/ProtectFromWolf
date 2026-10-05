# PROTECT FROM WOLF

> チーム制作物のうち、私の担当範囲を説明するために選定したC++コードだけを掲載しています。ゲーム本体、アセット、担当外または作成者を確認できないコードは含みません。

## 作品概要

「PROTECT FROM WOLF」は、銃を使う人型のFPS操作と、人狼へ変身した後のTPS近接戦闘を切り替えて戦うUnreal Engine 5製のアクションゲームです。本リポジトリはゲーム本体の配布物ではなく、私が担当したと確認できるC++実装を採用担当者へ説明するためのコード紹介用資料です。

- 制作期間: 約3か月（2025年11月20日 - 2026年2月13日）
- 制作体制: チーム制作（プログラマー3名）
- 使用技術: Unreal Engine 5 / C++（現在の改修版はUE 5.7.4）
- 公開範囲: 担当を確認したC++コードと説明資料のみ
- プレイ動画: [Google Driveで見る](https://drive.google.com/file/d/1TVYpkikDlaY0mise30JveE5y7Ndnf8Xr/view?usp=drive_link)
- 作品資料: [Google Driveで見る](https://drive.google.com/drive/folders/1lv9nxbNX8tpTkDl2XpudB7sLtmPInEcj)

### 制作後の改修について

手元のソースには、制作期間終了後に行った不具合修正、責務分割、コメント整理が含まれます。応募時には制作期間中の実装と、その後のポートフォリオ向け改修を区別して説明します。

### 2026年10月6日：担当範囲の確認と掲載拡張

本人の確認に基づき、敵専用武器・敵側の戦闘処理・AI・敵システム・プレイヤーHUD・タイトル／クリア／ゲームオーバーの処理へ拡張しました。現在はC++関連59ファイルです。詳しい掲載・除外理由は[掲載範囲](Source/README.md)を参照してください。

| 分野 | 主なソース |
| --- | --- |
| 敵の銃・弾・近接武器 | [Weapons](Source/ProtectFromWolf/Private/Weapons) |
| 敵の戦闘・ボス技 | [EnemyCharaCombat.cpp](Source/ProtectFromWolf/Private/Enemy/EnemyCharaCombat.cpp)、[EnemyCharaBoss.cpp](Source/ProtectFromWolf/Private/Enemy/EnemyCharaBoss.cpp)、[Combat](Source/ProtectFromWolf/Private/Combat) |
| 認識・捜索・行動遷移 | [AI](Source/ProtectFromWolf/Private/AI) |
| 状態・体力・遮蔽物・連携 | [Enemy](Source/ProtectFromWolf/Private/Enemy) |
| プレイヤーHUD・回避表示 | [PlayerUI.cpp](Source/ProtectFromWolf/Private/UI/PlayerUI.cpp) |
| タイトル・クリア・ゲームオーバー | [SciFiScreenSubsystem.cpp](Source/ProtectFromWolf/Private/UI/SciFiScreenSubsystem.cpp)、[SciFiScreenWidget.cpp](Source/ProtectFromWolf/Private/UI/SciFiScreenWidget.cpp) |
| タイトルの人物・開始演出 | [MenuCharacterStage.cpp](Source/ProtectFromWolf/Private/UI/MenuCharacterStage.cpp) |

対応するヘッダーは`Public`に掲載しています。画像・モデルなどの元アセットや、敵・ボスのHPウィジェットは今回の追加対象に含めません。

### 2026年10月6日の実行データ更新

- 実行データ更新時点では既存の担当コード5実装・10ファイルを同期し、その後、上記59ファイルへ掲載範囲を拡張しました。
- ゲーム本体のジャスト回避UIを小型化し、最後に操作した機器に合わせてキーボードの「ALT」／ゲームパッドの「B」を表示します。Xbox形式の表記であり、機種別アイコンの自動識別ではありません。
- スティックの微小入力による表示の切り替わりを抑えています。
- 担当範囲と追加改修の区別は[担当範囲](docs/担当範囲_20261006.txt)を参照してください。
- 敵の密集と足滑りは未解決の条件が残っています。全戦闘を不具合なく検証済みとはしていません。

### 2026年9月26日の更新履歴

- 公開済みの主要5実装・10ファイルを、UE 5.7移行後の開発プロジェクトと一致させました。
- メンバー変数は`m_`、引数は`_`で始める命名へ整理しました。エンジンが定義する名前は維持しています。
- 変身に必要なメッシュ・アニメーション・コンポーネントの確認を追加し、不足した状態での切り替えを防いでいます。
- ゲーム側ではタイトルの歩行演出、人物を回り込むカメラ、TPSからFPSへの接続も更新しています。この演出用コードは、従来の担当範囲の抜粋には含めていません。

パッケージ作成と実画面での確認範囲は[更新・確認記録](docs/RELEASE_20260926.md)に記載しています。全戦闘の通し検証を完了したという意味ではありません。

## ゲームの入手と起動

**[Windows版のリリース一覧](https://github.com/prashant-rayamajhi/ProtectFromWolf/releases)**

Assetsの`ProtectFromWolf-Windows-2026.10.06.zip`を選び、ZIP全体を展開して`Windows/ProtectFromWolf.exe`を起動してください。実行ファイルだけを別の場所へ移動せず、Engine・ProtectFromWolfフォルダも一緒に保持してください。

GitHubの「Code → Download ZIP」やリリース欄の「Source code (zip)」はソース紹介資料です。ゲーム本体ではなく、この抜粋だけではビルドも起動もできません。Windowsゲーム本体は、ソースとは別にReleasesへ掲載しています。

### 基本操作

| 操作 | キーボード・マウス | コントローラー（Xbox表記） |
| --- | --- | --- |
| 移動 | W / A / S / D | 左スティック |
| 視点 | マウス | 右スティック |
| 射撃・人狼の攻撃 | 左クリック | RT |
| 照準 | 右クリック | LT |
| リロード | R | X |
| ジャンプ | Space | A |
| 変身 | 左Shift | RB |
| ジャスト回避 | 左Alt | B |

変身はゲージ、ジャスト回避は回避可能なタイミングが必要です。操作表は入力設定を確認したものです。実コントローラーでの操作感は未検証です。

## 私の担当範囲

本人申告とコードの責務が一致し、コード紹介用として選定した範囲は次のとおりです。チーム制作のため、担当外または作成者を特定できないコードは掲載していません。

- プレイヤーキャラクターの一部
- 人型と人狼を切り替える変身処理
- 銃の射撃処理の一部
- 通常敵と中間ボス、ラストボスを含む敵キャラクター全般
- 敵AIの知覚、状態管理、攻撃判断、移動、遮蔽物利用
- 敵専用の武器と敵側の戦闘処理
- プレイヤーHUD、タイトル・ゲームクリア・ゲームオーバー画面の処理

## 担当外・掲載しない範囲

- プレイヤーキャラクターのうち担当を確認できない部分
- 射撃システムのうち担当を確認できない部分
- 敵・ボスのHPウィジェット、戦闘段階UIなど、今回の確認範囲に含まれないUI処理
- マップ、音楽、効果音、3Dモデル、アニメーション等の作成者不明部分
- チームメンバーが作成した非公開コード
- Unreal Engineの生成物、キャッシュ、パッケージ済みゲーム
- 再配布許可を確認できないBlueprintとアセット

## 1. 人型と人狼の変身処理

変身ではMeshを交換するだけでなく、体格差によってCollisionと床位置が変わる問題を扱いました。

### 発生していた問題

人型と人狼ではMesh BoundsとCapsule Componentの高さが異なります。Capsuleの中心を固定したままHalf Heightを変更すると底面が上下し、人狼が空中に浮く、または下半身が床へ埋まる状態が発生しました。また、人狼Meshでは尻尾等がBounds下端へ影響し、参照Boundsだけでは足元を正しく決められませんでした。

### 解決方法

1. 初回変身時に人型のCapsule半径、Half Height、Mesh Transformを保存します。
2. 人狼Mesh Boundsから必要なCapsule寸法を計算します。
3. Half Heightの差だけActor位置を移動し、変身前後でCapsule底面を維持します。
4. AnimInstance適用後にBone TransformとBoundsを更新します。
5. 左右の足Boneを優先して足元を求め、Capsule下端との差をMesh位置へ反映します。
6. 解除時は保存した人型のCollision、Mesh、Animation Blueprint、移動速度、ジャンプ性能を復元します。

初回値を一度だけ保存し、その基準値へ戻すため、変身と解除を繰り返しても補正値が累積しません。

主要コード:

- [`WerewolfFormComponent.cpp`](Source/ProtectFromWolf/Private/Player/WerewolfFormComponent.cpp)
- [`WerewolfFormComponent.h`](Source/ProtectFromWolf/Public/Player/WerewolfFormComponent.h)

## 2. ボスAIの攻撃選択

ボスの武器選択では、近接、銃、レーザーを固定したif文の順番だけで決めず、それぞれを独立した関数で採点します。

- プレイヤーとの距離
- プレイヤーの人狼状態、照準、静止傾向、攻撃傾向
- ボスが受けている圧力
- 周囲の味方数
- 銃の残弾率と弾切れ
- プレイヤーの残り体力

各候補が独立して点数を返し、最後に最大値を比較するため、新しい条件を追加した位置だけで結果が変わりにくい構造です。攻撃そのものはCooldownと直前の行動を確認し、同じ大技の連続使用を抑えます。

主要コード:

- [`EnemyDecisionComponent.cpp`](Source/ProtectFromWolf/Private/Enemy/Components/EnemyDecisionComponent.cpp)
- [`EnemyDecisionComponent.h`](Source/ProtectFromWolf/Public/Enemy/Components/EnemyDecisionComponent.h)
- [`EnemyCombatMemoryComponent.cpp`](Source/ProtectFromWolf/Private/Enemy/Components/EnemyCombatMemoryComponent.cpp)
- [`EnemyCombatMemoryComponent.h`](Source/ProtectFromWolf/Public/Enemy/Components/EnemyCombatMemoryComponent.h)

## 3. 通常敵のAI

AI Controller、AI Perception、Behavior Tree、Blackboardを使い、認識と行動を分離しています。

1. AI Perceptionがプレイヤーの視覚・聴覚刺激を受け取ります。
2. ControllerがTarget Actorと最後に確認した位置を保持します。
3. Serviceが距離、射線、体力、攻撃可能状態をBlackboardへ更新します。
4. Decoratorが死亡、停止、退避、攻撃範囲等の条件を判定します。
5. Taskが追跡、捜索、攻撃、再配置、巡回を実行します。
6. 攻撃やリロードが一定時間で終了しない場合は、停滞回復処理でIdleへ戻します。

近距離敵は有効な攻撃距離と方向を確認してから攻撃し、遠距離敵はNavMesh、射線、遮蔽物、味方との距離を評価して位置を選びます。

主要コード:

- [`EnemyAIController.cpp`](Source/ProtectFromWolf/Private/AI/Controllers/EnemyAIController.cpp)
- [`EnemyAIController.h`](Source/ProtectFromWolf/Public/AI/Controllers/EnemyAIController.h)
- [`EnemyBehaviorTasks.cpp`](Source/ProtectFromWolf/Private/AI/BehaviorTree/Tasks/EnemyBehaviorTasks.cpp)
- [`EnemyBehaviorTasks.h`](Source/ProtectFromWolf/Public/AI/BehaviorTree/Tasks/EnemyBehaviorTasks.h)

## 4. プレイヤーと射撃処理の連携

射撃処理は一部担当です。入力、照準、弾生成、リロード、UI更新が複数ファイルにまたがり、現存するGit履歴では行単位の作成者を分離できません。そのため、共有ファイル全体を私の成果として掲載しません。担当行をチーム内で確認できた場合のみ、該当部分を抜粋して説明します。


現在のコードでは、Blackboard値、Action State、攻撃候補ごとのScore関数を分けているため、ブレークポイントとWatchで条件を段階的に確認できます。今後ログを追加する場合は、Development Build限定のLog Categoryを用意し、候補名、各Score、採用理由、除外理由を1回の判断ごとに出力します。Shipping Buildでは無効化し、プレイ性能と内部情報を守ります。

## 主要コードの見どころ

詳細は [`docs/CODE_WALKTHROUGH.md`](docs/CODE_WALKTHROUGH.md) にまとめています。
