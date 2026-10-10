#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Enemy/EnemyData.h"
#include "EnemyChara.generated.h"

//敵がダメージを受けたことをAIとUIへ伝えるマルチキャストデリゲート
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDamaged);
//中間ボスが退避フェーズへ移行したことを戦闘進行へ伝えるマルチキャストデリゲート
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMiddleBossRetreat);

//敵キャラクターが使用する戦闘とAIと演出クラスの宣言
class UMeleeComponent;
class UWidgetComponent;
class AEnemyGun;
class APlayerChara;
class UNiagaraSystem;
class UNiagaraComponent;
class AEnemyMeleeWeapon;
class UEnemyHealthComponent;
class UEnemyStateComponent;
class UEnemyDecisionComponent;
class UEnemyCombatMemoryComponent;
class UEnemyCoverComponent;
class UAnimSequence;
class USoundBase;

//ボスのフェーズを定義する列挙型
UENUM(BlueprintType)
enum class EBossPhase : uint8
{
	Phase1 UMETA(DisplayName = "Phase 1"),
	Phase2 UMETA(DisplayName = "Phase 2"),
	Phase3 UMETA(DisplayName = "Phase 3")
};

//キャラクターの現在のアクション状態を管理する列挙型
UENUM(BlueprintType)
enum class EActionState : uint8
{
	Idle,
	Attacking,
	Reloading,
	Stunned,
	DrawingWeapon,
	AimingWeapon,
	HolsteringWeapon
};

//キャラクターが所持している武器の状態を管理する列挙型
UENUM(BlueprintType)
enum class EWeaponState : uint8
{
	Holstered,
	Drawing,
	Ready,
	Holstering
};

//敵の状態と装備と攻撃とボス専用行動を管理するクラス
UCLASS()
//敵の体力、武器、攻撃、ボス固有行動を統括するキャラクター
class PROTECTFROMWOLF_API AEnemyChara : public ACharacter
{
	GENERATED_BODY()
	friend class FEnemyDelayedCancelTest;
	friend struct FEnemyVisualReviewHarness;
	friend class FEnemyWeaponRestoreTest;

	//ボスと近接雑魚の通常歩行にだけ適用する速度倍率
	static constexpr float m_walkScale = 0.85f;

  public:
	//敵のHP、認識、攻撃、Animation Componentを初期化する関数
	AEnemyChara();
	//フレーム時間を使って継続中の移動、戦闘、演出を更新する関数
	virtual void Tick(float _deltaTime) override;
	//着地した瞬間だけ跳躍攻撃の衝撃を発生させる関数
	virtual void Landed(const FHitResult &_hit) override;
	//溜め姿勢を再生できる跳躍攻撃が設定されているか調べる関数
	bool CanGroundSmash() const;
	//まだ銃Actorを生成していない敵も、設定済みの銃を選択候補にできるか返す関数
	bool CanEquipGun() const;

	//攻撃がヒットした際にアニメーション通知から呼ばれる関数
	UFUNCTION(BlueprintCallable)
	void OnAttackHit();

  public:
	//近接攻撃によるダメージを受ける処理
	void TakeDamageMelee(int _damage, AActor *_damageCauser = nullptr);

	//攻撃を実行する関数
	void PerformAttack();

	//攻撃前に敵の正面を攻撃対象へ向ける関数
	void FaceTarget(const AActor *_target);

	static void BroadcastPlayerNoise(UObject *_worldContext, AActor *_player, const FVector &_noiseLocation, float _hearingRange);

	//予備弾薬からマガジンへ必要数を補充する関数
	void PeformReload();

	//近接攻撃の命中判定を開始する関数
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	void OpenMeleeHitWindow();

	//近接攻撃の命中判定を終了する関数
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	void CloseMeleeHitWindow(bool _finishSwing = false);

	//リロード状態を設定する関数
	void SetReloadState(bool _isReloading);

	//行動状態を設定する関数
	void SetActionState(EActionState _newState);
	//攻撃と移動を中断してキックの速度を一度だけ与える関数
	void ApplyKnockback(const FVector &_velocity);
	//吹き飛ばされた敵の着地待ちが続いているか判定する関数
	bool IsKnockedBack() const;

	//敵の武器を手のSocketへ装備する関数
	void EquipWeapon(EEnemyAttackStyle _newStyle);

	//銃を待機位置から照準位置へ移して構え始める関数
	void DrawWeapon();

	//敵の武器を待機位置へ戻す関数
	void HolsterWeapon();

	//銃をプレイヤーへ向けて射撃可能な照準状態へ移行する関数
	void AimWeapon();

	//射撃中を停止する関数
	void StopFiring();

	//武器を下ろす関数
	void LowerWeapon();

	//状態取得用の関数群
	bool IsReloading() const
	{
		return m_actionState == EActionState::Reloading;
	}
	bool IsAttacking() const
	{
		return m_actionState == EActionState::Attacking;
	}
	//Retreatが現在成立しているかを判定する関数
	bool ShouldRetreat() const;
	float GetChaseRange() const
	{
		return m_chaseRange;
	}
	//現在装備している武器の有効攻撃距離を返す関数
	float GetAttackRange() const;
	//追跡と攻撃開始で同じ武器の実射程を参照する関数
	float GetEffectiveAttackRange(const AActor *_target) const;
	float GetPlayerAttackDamage() const
	{
		return m_enemyRank == EEnemyRank::Minion ? 3.f : 5.f;
	}
	//ボスの射撃とレーザーに共通する再生速度と攻撃頻度の倍率を返す関数
	float GetRangedAttackRate() const
	{
		return m_enemyRank == EEnemyRank::Minion ? 1.f : 1.2f;
	}
	float GetProjectileAttackDamage() const
	{
		return m_enemyRank == EEnemyRank::Minion ? 1.f : 5.f;
	}
	//対象のCollision幅を含めた近接攻撃の命中可能距離を返す関数
	float GetMeleeStrikeRange(const AActor *_target) const;
	//剣の攻撃だけを中断し、レーザーや跳躍の攻撃状態と区別するためのフラグ
	bool m_meleeStrikeActive = false;
	//攻撃対象 Within 近接攻撃 Strike 範囲が現在成立しているかを判定する関数
	bool IsTargetWithinMeleeStrikeRange(const AActor *_target) const;
	//高い足場やジャンプ中の相手へ近接武器が届く高さか確認する関数
	bool CanReachMeleeHeight(const AActor *_target) const;
	//Commit 近接攻撃 攻撃が現在成立しているかを判定する関数
	bool CanCommitMeleeAttack(const AActor *_target) const;
	//最大体力に対する現在体力の割合を返す関数
	float GetHealthRatio() const;
	//敵の現在体力を返す関数
	float GetCurrentHealth() const;
	//敵Rankに応じて設定された最大体力を返す関数
	float GetMaxHealth() const;
	EActionState GetActionState() const
	{
		return m_actionState;
	}
	AEnemyGun *GetCurrentGun() const
	{
		return m_currentGun;
	}
	EWeaponState GetWeaponState() const
	{
		return m_weaponState;
	}
	bool IsWeaponReady() const
	{
		return m_weaponState == EWeaponState::Ready;
	}
	bool CanSeePlayer() const
	{
		return m_canSeePlayer;
	}
	void SetCanSeePlayer(bool _canSee)
	{
		m_canSeePlayer = _canSee;
	}

	//戦闘か判定する関数
	UFUNCTION(BlueprintPure, Category = "Enemy|Combat")
	bool IsInCombat() const;

	//戦闘戦闘姿勢を取得する関数
	UFUNCTION(BlueprintPure, Category = "Enemy|Combat")
	EEnemyCombatPosture GetCombatPosture() const;

	//ボスが攻撃の合間に停止している間だけ戦闘待機アニメーションを再生する関数
	void UpdateCombatIdleAnimation();
	//戦闘待機アニメーションを停止する関数
	void StopCombatIdleAnimation();

	//テレポート中かどうかを取得する処理を行う関数
	bool IsTeleporting() const
	{
		return m_isTeleporting;
	}

	//体力を全回復する処理を行う関数
	void RestoreFullHealth();

	//中間ボスの退避行動を有効または無効にする関数
	void SetCanRetreat(bool _canRetreat);

	//事前生成した敵を戦闘開始まで待機させる関数
	void SetCombatPreloaded(bool _preloaded);

	//ラストボス戦の間だけボスHPを表示する関数
	void SetBossHealthUIVisible(bool _visible);

	//武器をリロードする処理を行う関数
	void ReloadWeapon();

	//雑魚敵を召喚する処理を行う関数
	void SummonMinions();
	//召喚中に射撃停止や遮蔽物移動が割り込まないよう再生状態を調べる関数
	bool IsSummoning() const;

	//武器の切り替えを行う処理を行う関数
	void SwitchWeapon(EEnemyAttackStyle _newStyle);

	//ボス専用のゲッターとスキル実行処理を行う関数
	EBossPhase GetBossPhase() const;
	//地面スマッシュを実行する関数
	void PerformGroundSmash();
	//弾幕射撃を実行する関数
	void PerformBarrageShot();

	//跳躍攻撃を実行する関数
	void PerformLeapAttack();

	//速度を元に戻す処理を行う関数
	void RestoreDefaultSpeed();

	//退避処理を行う関数
	void PerformRetreat();

	//瞬間移動へのターゲットを実行する関数
	void PerformTeleportToTarget();

	//瞬間移動離れるからのターゲットを実行する関数
	void PerformTeleportAwayFromTarget();

	//レーザー溜めを開始する関数
	void StartLaserCharge();

	//レーザー溜めを停止する関数
	void StopLaserCharge();

	//レーザー攻撃手順を開始する関数
	void BeginLaserAttackSequence();
	//溜めまたは照射の途中に通常射撃の移動判断を割り込ませないための判定関数
	bool IsLaserSequenceActive() const;

	//レーザー開始前に武器の収納完了を待っている状態
	bool m_laserAfterHolster = false;
	//叩きつけ攻撃の前に武器収納を待つ状態
	bool m_smashAfterHolster = false;
	//近接武器への変更後、一度だけ接近する予約
	bool m_meleeAfterDraw = false;
	//実際にダメージを与えた一振りだけ次のコンボへ接続する関数
	void ConfirmMeleeHit();
	//コンボの各区間で命中判定を初期化する関数
	void UpdateMeleeSection();

  public:
	//現在の攻撃形式に対応する攻撃モンタージュを再生する関数
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void PlayAttackMontage();
	//近接モーションの正常終了と中断を区別して処理する関数
	void BindMeleeMontageEnd();

	//攻撃アニメーションの終了時に呼ばれる処理を行う関数
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void OnAttackEnd();

	//射撃シーケンスを開始する処理を行う関数
	UFUNCTION(BlueprintCallable)
	void BeginFireSequence();
	//射撃通知から停止通知までの区間で銃を構えているか確認する関数
	bool IsInGunFireWindow() const;

	//リロードの完了処理を行う関数
	UFUNCTION(BlueprintCallable)
	void FinishReload();

	//武器を抜く動作の完了通知関数
	UFUNCTION(BlueprintCallable)
	void OnWeaponDrawComplete();

	//武器を構える動作の完了通知関数
	UFUNCTION(BlueprintCallable)
	void OnWeaponAimComplete();

	//武器をしまう動作の完了通知関数
	UFUNCTION(BlueprintCallable)
	void OnWeaponHolsterComplete();

  protected:
	//ゲーム開始時に呼ばれる関数
	virtual void BeginPlay() override;

	//ダメージを受けた際に呼ばれる関数
	virtual float TakeDamage(float _damageAmount, FDamageEvent const &_damageEvent, AController *_eventInstigator, AActor *_damageCauser) override;

	//衝突判定時の関数
	void OnHit(UPrimitiveComponent *_hitComponent, AActor *_otherActor, UPrimitiveComponent *_otherComponent, FVector _normalImpulse,
			   const FHitResult &_hit);

	//アクターが破棄される時に呼ばれる処理を行う関数
	virtual void Destroyed() override;

	//各攻撃パターンの内部関数
	void MeleeAttack();
	//銃 攻撃に対応するクラス状態を更新する関数
	void GunAttack();
	//レーザー攻撃 攻撃に対応するクラス状態を更新する関数
	void LaserAttack();
	//Charged レーザー攻撃 攻撃 を現在の攻撃対象へ実行する関数
	void ExecuteChargedLaserAttack();
	//レーザー攻撃 攻撃を終了し、タイマーと一時状態を解除する関数
	void FinishLaserAttack();
	//レーザー本体と予兆Effectを停止して参照を解放する関数
	void CleanupLaserEffect();
	//一時Effectを指定時間後に安全に停止するTimerを設定する関数
	void ArmTransientEffectCleanup(UNiagaraComponent *_effect, float _maximumLifetime);
	//表示期限を過ぎたボスエフェクトを停止して一覧から削除する関数
	void CleanupTransientBossEffects();
	//全一時ボスエフェクト一覧を破棄する関数
	void DestroyAllTransientBossEffects();

	//武器の表示状態を切り替える処理を行う関数
	void SetWeaponVisibility(bool _bVisible);

	//ボスの初期攻撃パターンを決定する関数
	void DecideInitialAttackPattern();

	//武器を生成する関数
	void CreateWeapon(EEnemyAttackStyle _weaponType);

	//武器切り替え完了時の関数
	void OnWeaponSwitchComplete();

	//召喚アニメーション後に実際に雑魚を生成する内部関数
	void ExecuteSummon();

  public:
	//近接攻撃の判定を行うコンポーネント
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
	UEnemyHealthComponent *m_healthComponent;

	//攻撃、移動、待機など現在の行動状態を管理するコンポーネント
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
	UEnemyStateComponent *m_stateComponent;

	//距離や戦況から次の行動候補を選択するコンポーネント
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
	UEnemyDecisionComponent *m_decisionComponent;

	//プレイヤーの行動傾向と直前の戦闘結果を記録するコンポーネント
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
	UEnemyCombatMemoryComponent *m_combatMemoryComponent;

	//射線を遮る遮蔽物と回避位置を検索するコンポーネント
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Components")
	UEnemyCoverComponent *m_coverComponent;

	//人間形態の近接攻撃を管理するComponent
	UPROPERTY(VisibleAnywhere) UMeleeComponent *m_meleeComp;

	//敵のランク
	UPROPERTY(EditAnywhere, Category = "Rank") EEnemyRank m_enemyRank;

	//撃破したプレイヤーへ加算する変身ゲージ量。0の場合は敵ランクから自動設定する
	UPROPERTY(EditAnywhere, Category = "Reward", meta = (ClampMin = "0"))
	int32 m_enhanceGaugeReward;

	//最大体力
	UPROPERTY(EditAnywhere, Category = "Status") float m_maxHealth;

	//追跡範囲
	UPROPERTY(EditAnywhere, Category = "AI") float m_chaseRange;

	//アニメーションモンタージュを管理する
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	TMap<FName, UAnimMontage *> m_montageMap;

	//ボスが攻撃選択中に構えを維持する戦闘待機アニメーション
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Combat Idle")
	TObjectPtr<UAnimSequence> m_combatIdleAnimation;
	//射撃の合間も銃を構え続ける待機モーション
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Combat Idle")
	TObjectPtr<UAnimSequence> m_gunGuardAnimation;
	//抜刀後の構えを近接戦闘の待機姿勢に使うモーション
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Combat Idle")
	TObjectPtr<UAnimSequence> m_swordGuardAnimation;
	//現在表示している待機姿勢の武器種
	EEnemyAttackStyle m_idleStyle = EEnemyAttackStyle::Melee;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Animation|Boss Laser")
	//レーザー発射前の溜め動作として再生するAnimation Sequence
	TObjectPtr<UAnimSequence> m_laserChargeAnimation;

	//再生中の攻撃モンタージュ
	UAnimMontage *m_attackMontage;

	//現在の攻撃のインデックス
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation")
	int32 m_currentAttackIndex;

	//召喚する雑魚敵のクラス配列
	UPROPERTY(EditDefaultsOnly, Category = "Summon")
	TArray<TSubclassOf<AEnemyChara>> m_minionClass;

	//ボスかどうかのフラグ
	UPROPERTY(EditAnywhere, Category = "Boss")
	bool m_isBoss;

	//敵のデータを格納するデータテーブル
	UPROPERTY(EditAnywhere, Category = "Data")
	UDataTable *m_enemyData;

	//データテーブル内の行の名前
	UPROPERTY(EditAnywhere, Category = "Data")
	FName m_enemyRowName;

	//現在の攻撃スタイル
	UPROPERTY(EditAnywhere, Category = "AI")
	EEnemyAttackStyle m_currentStyle;

	//ダメージ発生時のデリゲート
	UPROPERTY(EditAnywhere, Category = "Health")
	FOnDamaged m_onDamaged;

	//中間ボスの退避時のデリゲート
	UPROPERTY(BlueprintAssignable, Category = "Boss")
	FOnMiddleBossRetreat m_onMiddleBossRetreat;

	//召喚を実行したかどうかのフラグ
	bool m_hasSummoned;

	//武器を切り替え中かどうかのフラグ
	bool m_isSwitchingWeapon;

  protected:
	//ボスの現在のフェーズ
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Boss AI")
	EBossPhase m_bossPhase;

	//体力ゲージを表示するUIコンポーネント
	UPROPERTY(VisibleAnywhere)
	UWidgetComponent *m_healthWidgetComp;

	//現在のアクション状態
	UPROPERTY(VisibleAnywhere, Category = "State")
	EActionState m_actionState;
	//action 状態 経過の開始、継続、終了を秒単位で判定する時間設定
	float m_actionStateElapsed;

	//現在の武器の状態
	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	EWeaponState m_weaponState;

	//武器操作の中断と完了を、その操作を開始したモンタージュだけで判定する変数
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> m_weaponMontage;
	//取り出しアニメーションの四フレーム目に武器を表示するための再生位置
	float m_weaponShowTime = 0.f;
	//収納動作で銃が背中へ回った時点を指定する再生位置の割合
	UPROPERTY(EditDefaultsOnly, Category = "Animation|Weapon", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float m_gunHideRatio = 0.4f;
	//収納モンタージュで手元の武器を非表示にする再生時刻
	float m_weaponHideTime = 0.f;
	//体力条件を満たした召喚を現在の攻撃や装備変更の完了後に一度だけ行う変数
	bool m_summonPending = false;

	//武器操作の再生位置から表示と中断後の復帰を更新する関数
	void UpdateWeaponAnimation();

	//召喚時のエフェクト
	UPROPERTY(EditDefaultsOnly, Category = "Effects")
	UNiagaraSystem *m_summonEffect;

	//レーザー攻撃時のビームエフェクト
	UPROPERTY(EditDefaultsOnly, Category = "Effects")
	UNiagaraSystem *m_laserEffect;

	//グランドスマッシュ時のエフェクト
	UPROPERTY(EditDefaultsOnly, Category = "Effects")
	UNiagaraSystem *m_smashEffect;

	//地面叩きつけ半径
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Ground Smash", meta = (ClampMin = "50.0"))
	float m_groundSmashRadius;

	//地面叩きつけエフェクト基準半径
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Ground Smash", meta = (ClampMin = "1.0"))
	float m_groundSmashEffectReferenceRadius;

	//地面叩きつけが範囲内のプレイヤーへ与えるDamage
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Ground Smash", meta = (ClampMin = "0.0"))
	float m_groundSmashDamage;

	//ground Smash Impact Delayの開始・終了タイミングを秒単位で制御する
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Ground Smash", meta = (ClampMin = "0.0"))
	float m_groundSmashImpactDelay;

	//テレポート時のエフェクト（In / Out 兼用）
	UPROPERTY(EditDefaultsOnly, Category = "Effects")
	UNiagaraSystem *m_teleportEffect;

	//レーザー溜め中のグロウエフェクト用 NiagaraSystem
	UPROPERTY(EditDefaultsOnly, Category = "Effects")
	UNiagaraSystem *m_laserChargeGlowEffect;

	//レーザーの溜め中だけ保持し、発射や中断時に破棄する発光コンポーネント
	UPROPERTY(VisibleAnywhere, Category = "Effects")
	UNiagaraComponent *m_laserChargeComp;

	//溜め完了後から攻撃終了まで表示するレーザーエフェクト
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> m_activeLaserBeam;

	//レーザー溜め継続時間
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Laser", meta = (ClampMin = "0.1"))
	float m_laserChargeDuration;

	//ボスの位置からレーザー攻撃判定を伸ばす最大距離
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Laser", meta = (ClampMin = "100.0"))
	float m_laserRange;

	//レーザーの見た目とダメージ判定を一致させる半径
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Laser", meta = (ClampMin = "1.0"))
	float m_laserBeamRadius;

	//通常攻撃力からレーザーダメージを計算する倍率
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Laser", meta = (ClampMin = "0.1"))
	float m_laserDamageMultiplier;

	//レーザーエフェクト継続時間
	UPROPERTY(EditDefaultsOnly, Category = "Boss|Laser", meta = (ClampMin = "0.1"))
	float m_laserEffectDuration;

	//遠距離攻撃形式で生成して敵の手へ装備する銃のクラス
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	TSubclassOf<AEnemyGun> m_gunClass;

	//近接武器のクラス
	UPROPERTY(EditDefaultsOnly, Category = "Weapon")
	TSubclassOf<AActor> m_meleeWeaponClass;

	//切り替え予定の攻撃スタイル
	EEnemyAttackStyle m_pendingWeaponStyle;

	//元の移動速度を保存しておく変数
	float m_defaultMoveSpeed;

  protected:
	//現在の攻撃形式でプレイヤーへ攻撃できる最大距離
	float m_attackRange;

	//現在の敵ランクが一回の攻撃で与える基準ダメージ量
	float m_damage;

	//次の召喚を行う体力のしきい値
	float m_nextSummonThreshold;

	//召喚を行った回数
	int32 m_summonCount;

	//最後に召喚を行った際の体力割合
	float m_lastSummonHP;

  private:
	//攻撃状態をリセットする関数
	void ResetAttackState();
	//長時間完了しない攻撃や武器切り替えを解除して戦闘判断へ復帰する関数
	void RecoverFromStalledAction();

	//ターゲットの方向を向いているか判定する関数
	bool IsFacingTarget(const AActor *_enemy, const AActor *_target, float _facingAngleDegrees);

	//撃破したプレイヤーへ変身ゲージを付与する関数
	void AwardEnhanceGauge(AController *_eventInstigator, AActor *_damageCauser);
	//移動速度と接地状態に合わせて足音の再生間隔を更新する関数
	void UpdateFootstepAudio();

  private:
	//現在装備している銃
	UPROPERTY()
	AEnemyGun *m_currentGun;

	//現在装備している近接武器
	UPROPERTY()
	AActor *m_meleeWeapon;

  private:
	//体力コンポーネントと同期している敵の現在体力
	float m_currentHealth;

	//攻撃中かどうかのフラグ
	bool m_isAttacking;

	//退避したかどうかのフラグ
	bool m_hasRetreated;

	//敵の歩行音を交互再生する一つ目の足音
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> m_enemyFootstepA;

	//敵の歩行音を交互再生する二つ目の足音
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> m_enemyFootstepB;

	//近接攻撃振り効果音A
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> m_meleeSwingSoundA;

	//近接攻撃振り効果音B
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> m_meleeSwingSoundB;

	//ボスがレーザーを発射している間に再生する攻撃音
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> m_bossLaserSound;

	//地面叩きつけ効果音
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> m_groundSmashSound;

	//敵がプレイヤーからダメージを受けた時に再生する音
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> m_enemyDamageSound;

	//敵の体力がなくなり死亡処理を開始する時に再生する音
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> m_enemyDeathSound;

	//敵が銃を構える時または収納する時に再生する操作音
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> m_weaponHandlingSound;

	//足音音響タイマー管理用タイマー
	FTimerHandle m_footstepAudioTimerHandle;
	//使用交互足音かどうか
	bool m_useAlternateFootstep = false;
	//次の足音音響時刻
	float m_nextFootstepAudioTime = 0.f;
	//次のダメージ音響時刻
	float m_nextDamageAudioTime = 0.f;

	//戦闘空間では中間ボスを最後まで戦わせるための退避可否
	bool m_canRetreat;

	//攻撃がヒットしたかどうかのフラグ
	bool m_attackHit;

	//リロード中かどうかのフラグ
	bool m_isReloading;
	//近接攻撃命中受付時間開始かどうか
	bool m_meleeHitWindowOpen;
	//通知と補助タイマーが重なっても剣を振る音を一度だけ鳴らす状態
	bool m_meleeSwingPlayed = false;

	//プレイヤーが見えているかどうかのフラグ
	bool m_canSeePlayer;

	//初期攻撃が近接攻撃かどうかのフラグ
	bool m_startWithMelee;

	//テレポート中かどうかのフラグ
	bool m_isTeleporting;

	//撃破報酬を二重に付与しないためのフラグ
	bool m_enhanceRewardGranted;

	//テレポート遵延処理に使用するタイマー
	FTimerHandle m_teleportTimerHandle;
	//中断した瞬間移動の到着待ちを取り消すタイマー
	FTimerHandle m_teleportFinishTimer;
	//中断や死亡で援軍の生成予約を取り消すタイマー
	FTimerHandle m_summonTimer;

	//攻撃のクールダウンを管理するタイマー
	FTimerHandle m_attackCoolDown;
	//レーザー攻撃タイマー管理用タイマー
	FTimerHandle m_laserAttackTimerHandle;
	//レーザー状態初期化タイマー管理用タイマー
	FTimerHandle m_laserStateResetTimerHandle;
	//レーザーエフェクト後片付けタイマー管理用タイマー
	FTimerHandle m_laserEffectCleanupTimerHandle;
	//一時ボスエフェクト後片付けタイマー管理用タイマー
	FTimerHandle m_transientBossEffectCleanupTimerHandle;
	//リロード完了まで射撃を禁止するTimer
	FTimerHandle m_reloadTimerHandle;
	//近接攻撃モンタージュに合わせて命中受付を開始するTimer
	FTimerHandle m_meleeHitWindowOpenTimerHandle;
	//近接攻撃モンタージュに合わせて命中受付を終了するTimer
	FTimerHandle m_meleeHitWindowCloseTimerHandle;

	//transient ボス Effects を処理順または生成順に管理する配列
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> m_transientBossEffects;
	//transient ボス エフェクト Expiration Timesを処理順または生成順に管理する配列
	TArray<double> m_transientBossEffectExpirationTimes;

	//近接攻撃 命中 受付時間 Start 割合 の強さまたは進行度を表す割合
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Melee", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float m_meleeHitWindowStartRatio;

	//近接攻撃 命中 受付時間 End 割合 の強さまたは進行度を表す割合
	UPROPERTY(EditDefaultsOnly, Category = "Enemy|Melee", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float m_meleeHitWindowEndRatio;

	//戦闘中の待機姿勢に切り替えるAnimation Blueprintクラス
	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> m_combatIdleAnimClass;

	//戦闘用Idleモンタージュを再生中か示す状態
	bool m_playingCombatIdle;
	//移動開始時に戦闘待機だけを停止するための専用モンタージュ
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> m_idleMontage;

	//プレイヤーキャラクターへの参照
	APlayerChara *m_player;
	//ノックバック後に行動を再開できる最短時刻
	float m_knockbackUntil = -1.f;
	//叩きつけ攻撃を中断した時に衝撃の予約を解除するタイマー
	FTimerHandle m_smashTimer;
	//溜め開始から着地まで跳躍攻撃を中断可能にする状態
	bool m_smashPending = false;
	//通常の段差着地と攻撃による着地を区別する状態
	bool m_smashAirborne = false;
};
