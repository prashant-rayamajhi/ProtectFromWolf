# 掲載ソースについて

現在の掲載対象は46ファイルです。

- `Private/AI`・`Public/AI`：Controllerの分割実装、BTのTask・Service・Decorator、Blackboard。
- `Private/Enemy`・`Public/Enemy`：通常敵・ボス、戦闘、状態・体力・行動判断・戦闘記憶・遮蔽物・連携・テレポート配置。
- `Weapons/EnemyGun`・`EnemyBullet`・`EnemyMeleeWeapon`：敵専用の銃、弾、近接武器（cpp／h）。
- `Combat/FireComponent`・`MeleeComponent`：敵用の射撃・近接部品（cpp／h）。すべての攻撃がこれらだけで実行されるという意味ではありません。
- `Player/WerewolfFormComponent`：人型と人狼の変身・解除（cpp／h）。

プレイヤー用武器や共有戦闘部品、マップ・スポーナーなど、担当範囲を個別に確認していない部分は追加していません。依存する未掲載のクラスや素材があるため、このリポジトリ単独ではビルド・起動できません。

本人申告の担当範囲とファイルの責務を照合した掲載であり、Git履歴から全行の作成者を証明したものではありません。制作期間後の改修も含みます。
