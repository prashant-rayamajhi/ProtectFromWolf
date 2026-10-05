# 掲載ソースについて

2026年10月6日の本人確認に基づき、従来の10ファイルから59ファイルへ拡張しました。開発元とファイル内容の一致を照合しています。

- Private/AI・Public/AI：Controllerの分割実装、BTのTask・Service・Decorator、Blackboard。
- Private/Enemy・Public/Enemy：通常敵・ボス、戦闘、状態・体力・行動判断・戦闘記憶・遮蔽物・連携・テレポート配置。
- Weapons/EnemyGun・EnemyBullet・EnemyMeleeWeapon：敵専用の銃、弾、近接武器（cpp／h）。
- Combat/FireComponent・MeleeComponent：敵用の射撃・近接部品（cpp／h）。すべての攻撃がこれらだけで実行されるという意味ではありません。
- UI/PlayerUI：HUDと入力機器に応じた回避案内（cpp／h）。
- UI/TitleWidget・SciFiScreenWidget・SciFiScreenSubsystem・MenuCharacterStage：タイトル、結果画面、開始演出と遷移（cpp／h）。
- Private/UI/MenuBackdrop.h・Public/UI/MenuTravelState.h：背景描画と遷移状態。
- Private/Animation/EnemyRetreatLayer.cpp：敵の退避走行レイヤー。
- Player/WerewolfFormComponent：変身処理（cpp／h）。

## 今回含めない範囲

プレイヤー用Gun・Projectile、共有のAmmoReload・CombatComponent・AttackNotifyState・CombatTracer、人狼コンボ等は、敵の戦闘処理と一括して本人作成とは扱いません。マップ・戦闘空間の進行管理とスポーナー、音声共通部、敵HPウィジェット・PhaseDisplayも追加対象外です。他人作成と断定したものではなく、個別の担当確認を残した部分です。

本人申告の担当範囲とファイルの責務を照合した掲載であり、Git履歴から全行の作成者を証明したものではありません。制作期間後の改修も含みます。

チームメンバー作成の可能性がある共有基底クラス、Blueprint、DataTable、Animation、アセットは含めていません。そのため、このソースだけではコンパイルできません。コードの責務分割、状態管理、候補評価、問題解決方法を確認するための抜粋です。

本リポジトリはコード紹介用の抜粋です。依存する基底クラスやアセットを含まないため、このフォルダだけではコンパイルできません。
