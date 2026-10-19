# 掲載ソースについて

2026年10月19日更新。現在の掲載対象は54ファイルです。公開済み53ファイルを開発プロジェクトと照合し、差分のある`WerewolfFormComponent.cpp`と`.h`を更新しました。変身部品が使用する高さ計算の補助ヘッダー`JumpHeight.h`も追加しています。その他の掲載済みファイルには差分がありませんでした。

- `Private/AI`・`Public/AI`：Controllerの分割実装、BTのTask・Service・Decorator、Blackboard。
- `Private/Enemy`・`Public/Enemy`：通常敵・ボス、戦闘、状態・体力・行動判断・戦闘記憶・遮蔽物・連携・テレポート配置。
- `Weapons/EnemyGun`・`EnemyBullet`・`EnemyMeleeWeapon`：敵専用の銃、弾、近接武器（cpp／h）。
- `Combat/FireComponent`・`MeleeComponent`：敵用の射撃・近接部品（cpp／h）。すべての攻撃がこれらだけで実行されるという意味ではありません。
- `Player/WerewolfFormComponent`：人型と人狼の変身・解除（cpp／h）。
- `Public/Player/JumpHeight.h`：重力と高さ上限からジャンプ初速を制限する補助処理。人型側の初期化がある共有ファイル`PlayerChara.cpp`全体は掲載していません。
- `Private/Player/PlayerCharaMovement.cpp`：移動・視点・ジャンプ入力、足音と物音通知、変身開始と解除の連携。プレイヤー全体ではなく分割実装の一部です。対応する共有ヘッダー`PlayerChara.h`は今回の追加対象に含めていません。
- `Combat/AttackNotifyState`：敵と狼男の近接攻撃で、命中判定を受け付ける区間の開始・終了（cpp／h）。
- `Combat/CombAnimNotifyState`：狼男コンボの次の入力を受け付ける区間の開始・終了（cpp／h）。
- `Player/AnimNotify_ReloadComplete`：リロード動作の通知から弾薬補充を確定する処理への接続（cpp／h）。

UIとAnimationのフォルダ、アセット、テスト用コードは掲載していません。今回追加したAnimNotify関連はCombatとPlayerの元の配置を維持しています。

プレイヤー用武器や、今回指定された通知クラス以外の共有戦闘部品、マップ・スポーナーなど、担当範囲を個別に確認していない部分は追加していません。通知クラスを含め、依存する未掲載のクラスや素材があるため、このリポジトリ単独ではビルド・起動できません。

本人申告の担当範囲とファイルの責務を照合した掲載であり、Git履歴から全行の作成者を証明したものではありません。制作期間後の改修も含みます。
