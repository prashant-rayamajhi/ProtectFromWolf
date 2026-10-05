#include "Enemy/EnemyChara.h"
#include "Combat/MeleeComponent.h"
#include "Movement/CharacterPace.h"
#include "Audio/FootstepAttenuation.h"
#include "Combat/CombatComponent.h"
#include "Combat/MeleeAttackHitBox.h"
#include "Components/CapsuleComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Player/PlayerChara.h"
#include "AI/Controllers/EnemyAIController.h"
#include "Perception/AISense_Hearing.h"
#include "Weapons/EnemyGun.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Components/WidgetComponent.h"
#include "UI/BossEnemyWidget.h"
#include "UI/PlayerUI.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/EnemyMeleeWeapon.h"
#include "Engine/DamageEvents.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Enemy/Components/EnemyHealthComponent.h"
#include "Enemy/Components/EnemyStateComponent.h"
#include "Enemy/Components/EnemyDecisionComponent.h"
#include "Enemy/Components/EnemyCombatMemoryComponent.h"
#include "Enemy/Components/EnemyCoverComponent.h"
#include "Animation/AnimSequence.h"
#include "UObject/ConstructorHelpers.h"
#include "Sound/SoundBase.h"

//敵の移動と戦闘と知覚と体力に使用する初期値を設定する関数
AEnemyChara::AEnemyChara()
{
	//ティックを有効化
	PrimaryActorTick.bCanEverTick = true;

	//AIコントローラーのクラスを設定し、敵キャラクターに「Enemy」タグを追加する
	Tags.AddUnique(TEXT("Enemy"));

	//AIコントローラーのクラスを設定し、敵キャラクターがスポーン時に自動的にAIコントローラーを所有するように設定する
	AIControllerClass = AEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	//コンポーネントの作成と初期化
	m_meleeComp = CreateDefaultSubobject<UMeleeComponent>(TEXT("MeleeCommponent"));
	m_healthWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("LastBossHealthBar"));
	m_healthComponent = CreateDefaultSubobject<UEnemyHealthComponent>(TEXT("EnemyHealthComponent"));
	m_stateComponent = CreateDefaultSubobject<UEnemyStateComponent>(TEXT("EnemyStateComponent"));
	m_decisionComponent = CreateDefaultSubobject<UEnemyDecisionComponent>(TEXT("EnemyDecisionComponent"));
	m_combatMemoryComponent = CreateDefaultSubobject<UEnemyCombatMemoryComponent>(TEXT("EnemyCombatMemoryComponent"));
	m_coverComponent = CreateDefaultSubobject<UEnemyCoverComponent>(TEXT("EnemyCoverComponent"));

	//敵の初期パラメータを設定する
	m_enemyRank = EEnemyRank::Minion;
	m_maxHealth = 100.f;
	m_currentHealth = m_maxHealth;
	m_chaseRange = 2000.f;
	m_attackRange = 250.f;
	m_damage = 3.f;
	m_enhanceGaugeReward = 0;
	m_enhanceRewardGranted = false;
	m_isAttacking = false;
	m_isBoss = false;
	m_attackHit = false;
	m_isReloading = false;
	m_meleeHitWindowOpen = false;
	m_hasSummoned = false;
	m_hasRetreated = false;
	m_canRetreat = true;
	m_currentAttackIndex = 0;
	m_actionState = EActionState::Idle;
	m_actionStateElapsed = 0.f;
	m_weaponState = EWeaponState::Holstered;
	m_canSeePlayer = false;
	m_summonCount = 0;
	m_lastSummonHP = 1.0f;
	m_pendingWeaponStyle = EEnemyAttackStyle::Melee;
	m_isSwitchingWeapon = false;
	m_defaultMoveSpeed = 400.f;
	m_isTeleporting = false;
	m_playingCombatIdle = false;
	m_groundSmashRadius = 350.f;
	m_groundSmashEffectReferenceRadius = 350.f;
	m_groundSmashDamage = 5.f;
	m_groundSmashImpactDelay = 1.f;
	m_laserChargeDuration = 1.25f;
	m_laserRange = 5000.f;
	m_laserBeamRadius = 24.f;
	m_laserDamageMultiplier = 2.f;
	m_laserEffectDuration = 0.4f;
	m_activeLaserBeam = nullptr;
	m_meleeHitWindowStartRatio = 0.18f;
	m_meleeHitWindowEndRatio = 0.82f;

	//アセットの参照を取得する
	static ConstructorHelpers::FObjectFinder<UAnimSequence> combatIdleFinder(TEXT("/Game/Enemy/Animations/Boss_Animation/SK_Boss_Ile.SK_Boss_Ile"));
	if (combatIdleFinder.Succeeded()) { m_combatIdleAnimation = combatIdleFinder.Object; }

	//レーザー攻撃のチャージアニメーションを取得する
	static ConstructorHelpers::FObjectFinder<UAnimSequence> laserChargeAnimationFinder(
		TEXT("/Game/Enemy/Animations/Boss_Animation/Fireball.Fireball"));
	if (laserChargeAnimationFinder.Succeeded()) { m_laserChargeAnimation = laserChargeAnimationFinder.Object; }

	//音源の参照を取得する
	static ConstructorHelpers::FObjectFinder<USoundBase> enemyFootstepA(
		TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_EnemyFootstepA.SFX_EnemyFootstepA"));
	static ConstructorHelpers::FObjectFinder<USoundBase> enemyFootstepB(
		TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_EnemyFootstepB.SFX_EnemyFootstepB"));
	static ConstructorHelpers::FObjectFinder<USoundBase> meleeSwingA(TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_MeleeSwingA.SFX_MeleeSwingA"));
	static ConstructorHelpers::FObjectFinder<USoundBase> meleeSwingB(TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_MeleeSwingB.SFX_MeleeSwingB"));
	static ConstructorHelpers::FObjectFinder<USoundBase> bossLaser(TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_BossLaser.SFX_BossLaser"));
	static ConstructorHelpers::FObjectFinder<USoundBase> groundSmash(TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_GroundSmash.SFX_GroundSmash"));
	static ConstructorHelpers::FObjectFinder<USoundBase> beamImpact(TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_BeamImpact.SFX_BeamImpact"));

	//音源の参照を設定する
	m_enemyFootstepA = enemyFootstepA.Object;
	m_enemyFootstepB = enemyFootstepB.Object;
	m_meleeSwingSoundA = meleeSwingA.Object;
	m_meleeSwingSoundB = meleeSwingB.Object;
	m_bossLaserSound = bossLaser.Object;
	m_groundSmashSound = groundSmash.Object;
	m_enemyDamageSound = beamImpact.Object;
	m_enemyDeathSound = groundSmash.Object;
	m_weaponHandlingSound = meleeSwingB.Object;

	//武器の参照を初期化する
	m_currentGun = nullptr;
	m_meleeWeapon = nullptr;

	//ボスのフェーズを初期化する
	m_bossPhase = EBossPhase::Phase1;

	//キャラクターの回転と移動の設定を行う
	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent *movement = GetCharacterMovement())
	{
		movement->bOrientRotationToMovement = true;
		movement->bUseControllerDesiredRotation = false;
		movement->RotationRate = FRotator(0.f, 540.f, 0.f);
		movement->bUseRVOAvoidance = true;
		movement->AvoidanceConsiderationRadius = 450.f;
		movement->AvoidanceWeight = 0.55f;
	}
}

//毎フレームの状態を更新する関数
void AEnemyChara::Tick(float _deltaTime)
{
	//毎フレームの状態を更新する関数
	Super::Tick(_deltaTime);
	//経路移動が始まったフレームに待機姿勢を解除する
	if (m_playingCombatIdle && GetVelocity().SizeSquared2D() > FMath::Square(5.f)) { StopCombatIdleAnimation(); }
	//着地前に追跡や攻撃を再開して吹き飛び速度を消さない
	if (IsKnockedBack()) { return; }
	if (m_actionState == EActionState::Stunned) { SetActionState(EActionState::Idle); }

	//アクション状態がIdleまたは体力が0以下の場合、経過時間をリセットして処理を終了する
	if (m_actionState == EActionState::Idle || GetHealthRatio() <= 0.f)
	{
		m_actionStateElapsed = 0.f;
		return;
	}

	//アクション状態がIdle以外の場合、経過時間を加算する
	m_actionStateElapsed += _deltaTime;
	//正常に射撃姿勢を維持している連続バーストを停止故障と誤判定しない
	if (m_actionState == EActionState::Attacking && m_currentStyle == EEnemyAttackStyle::Gun && m_currentGun &&
		!m_currentGun->IsOutOfAmmo() && GetMesh()->GetAnimInstance() &&
		GetMesh()->GetAnimInstance()->Montage_GetCurrentSection(m_attackMontage) == TEXT("Fire"))
	{
		//一度の射撃姿勢を無期限に維持せず、戦況と遮蔽物を再評価する機会を作る
		const float sessionTime = m_enemyRank == EEnemyRank::Minion ? 4.5f : 3.f;
		if (m_actionStateElapsed >= sessionTime) { StopFiring(); }
		return;
	}

	//行動停止から自動復帰するまでの時間
	float timeout = 5.f;
	if (m_actionState == EActionState::Attacking && GetWorldTimerManager().IsTimerActive(m_laserAttackTimerHandle))
	{
		//銃や剣から選んだレーザーも、溜めと発射が終わる前に停止監視で中断しない
		timeout = FMath::Max(7.f, m_laserChargeDuration + m_laserEffectDuration + 1.f);
	}
	//リロードの弾薬状態に合わせて射撃または補充処理へ分岐する
	else if (m_actionState == EActionState::Reloading) { timeout = 6.f; }
	//アクション状態が一定時間以上続いた場合、強制的に回復処理を行う
	if (m_actionStateElapsed >= timeout) { RecoverFromStalledAction(); }
}

//攻撃や移動が停止した敵のタイマーと状態を解除し、AIの行動選択を再開する関数
void AEnemyChara::RecoverFromStalledAction()
{
	//吹き飛びや死亡の後に、以前の瞬間移動と召喚を実行させない
	GetWorldTimerManager().ClearTimer(m_teleportTimerHandle);
	GetWorldTimerManager().ClearTimer(m_teleportFinishTimer);
	GetWorldTimerManager().ClearTimer(m_summonTimer);
	//中断後の着地で遅れて衝撃を発生させない
	m_smashPending = false;
	m_smashAirborne = false;
	GetWorldTimerManager().ClearTimer(m_smashTimer);
	//アクション状態をIdleに設定し、経過時間をリセットする
	StopFiring();
	CloseMeleeHitWindow();
	StopLaserCharge();
	CleanupLaserEffect();
	GetWorldTimerManager().ClearTimer(m_reloadTimerHandle);
	GetWorldTimerManager().ClearTimer(m_laserAttackTimerHandle);
	GetWorldTimerManager().ClearTimer(m_laserStateResetTimerHandle);
	m_isReloading = false;
	m_isSwitchingWeapon = false;
	m_isTeleporting = false;
	m_weaponState = EWeaponState::Ready;
	RestoreDefaultSpeed();
	SetActionState(EActionState::Idle);
}

//ゲーム開始時に必要な参照を取得し、初期状態とイベント通知を設定する関数
void AEnemyChara::BeginPlay()
{
	//親クラスのBeginPlay関数を呼び出す
	Super::BeginPlay();

	//敵キャラクターに「Enemy」タグを追加し、ダメージを受けられるように設定する
	Tags.AddUnique(TEXT("Enemy"));
	SetCanBeDamaged(true);

	//AIコントローラーをスポーンし、敵キャラクターに所有させる
	if (HasAuthority() && !GetController() && !Tags.Contains(TEXT("PreloadedCombatEnemy")))
	{
		if (!AIControllerClass) { AIControllerClass = AEnemyAIController::StaticClass(); }
		SpawnDefaultController();
	}

	//敵キャラクターの足音を定期的に更新するタイマーを設定する
	GetWorldTimerManager().SetTimer(m_footstepAudioTimerHandle, this, &AEnemyChara::UpdateFootstepAudio, 0.12f, true, 0.12f);

	//敵キャラクターの攻撃エフェクトをSci-Fi風に置き換えるためのラムダ関数を定義する
	auto useSciFiEffect = [](UNiagaraSystem *&_effect, const TCHAR *_assetPath)
	{
		//エフェクトが未設定、または特定のパス名を含む場合に、指定されたアセットパスから新しいエフェクトをロードして置き換える
		if (!_effect || _effect->GetPathName().Contains(TEXT("/Free_Magic/")) || _effect->GetPathName().Contains(TEXT("NS_MuzzleFlash")))
		{
			if (UNiagaraSystem *replacement = LoadObject<UNiagaraSystem>(nullptr, _assetPath)) { _effect = replacement; }
		}
	};

	//各攻撃エフェクトをSci-Fi風に置き換える
	useSciFiEffect(m_summonEffect, TEXT("/Game/MixedVFX/Particles/Mix/NS_ElectricField.NS_ElectricField"));
	useSciFiEffect(m_laserEffect, TEXT("/Game/Enemy/Assets/NS_Beam.NS_Beam"));
	useSciFiEffect(m_smashEffect, TEXT("/Game/MixedVFX/Particles/Projectiles/Hits/NS_Projectile_05_Hit.NS_Projectile_05_Hit"));
	useSciFiEffect(m_teleportEffect, TEXT("/Game/Vefects/Zap_VFX/VFX/Zap/Particles/NS_Zap_06_White.NS_Zap_06_White"));
	useSciFiEffect(m_laserChargeGlowEffect, TEXT("/Game/MixedVFX/Particles/Mix/NS_ElectricField.NS_ElectricField"));
	m_combatIdleAnimClass = GetMesh() ? GetMesh()->GetAnimClass() : nullptr;

	//敵データテーブルから敵のパラメータを取得し、初期化する
	if (m_enemyData)
	{
		static const FString contextString(TEXT("Enemy Data Context"));
		//DataTable行へ安全にアクセスする参照
		FEnemyData *rowData = m_enemyData->FindRow<FEnemyData>(m_enemyRowName, contextString);

		//敵データが見つかった場合、敵のパラメータを設定する
		if (rowData)
		{
			m_maxHealth = rowData->m_maxHealth;
			m_currentHealth = m_maxHealth;
			GetCharacterMovement()->MaxWalkSpeed = rowData->m_moveSpeed;

			//デフォルトの移動速度を設定する
			m_defaultMoveSpeed = rowData->m_moveSpeed;
			m_currentStyle = rowData->m_attackStyle;
			m_enemyRank = rowData->m_enemyRank;
			m_attackRange = rowData->m_attackRange;
			m_damage = rowData->m_attackPower;
			m_chaseRange = rowData->m_chaseRange;

			//ダメージが100以上の場合、10にリセットする
			if (m_damage >= 100.0f) { m_damage = 10.0f; }

			//近接攻撃コンポーネントが存在する場合、ダメージと攻撃範囲を設定する
			if (m_meleeComp)
			{
				m_meleeComp->SetDamage(m_damage);
				m_meleeComp->SetAttackRange(250.f);
			}
		}
	}

	//ボスと近接雑魚の通常移動を15％抑え、歩行の再生速度は実際の移動速度に追従させる
	if (m_enemyRank != EEnemyRank::Minion || m_currentStyle == EEnemyAttackStyle::Melee)
	{
		m_defaultMoveSpeed *= m_walkScale;
	}
	//前回の近接敵調整を維持したうえで、全ての敵の通常移動をさらに10％遅くする
	m_defaultMoveSpeed *= CharacterPace::WalkScale;
	GetCharacterMovement()->MaxWalkSpeed = m_defaultMoveSpeed;

	//古いデータ値に左右されないよう敵ランクからプレイヤーへのダメージを決定する処理
	m_damage = GetPlayerAttackDamage();
	m_groundSmashDamage = GetPlayerAttackDamage();
	if (m_meleeComp) { m_meleeComp->SetDamage(m_damage); }

	//敵の体力コンポーネントを初期化する
	if (m_healthComponent) { m_healthComponent->InitializeHealth(m_maxHealth, m_enemyRank, m_canRetreat); }

	//敵の強化ゲージ報酬を設定する
	if (m_enhanceGaugeReward <= 0)
	{
		//現在の状態に対応する処理へ分岐して挙動を切り替える
		switch (m_enemyRank)
		{
		case EEnemyRank::Minion:
			m_enhanceGaugeReward = 20;
			break;
		case EEnemyRank::MiddleBoss:
			m_enhanceGaugeReward = 50;
			break;
		case EEnemyRank::LastBoss:
			m_enhanceGaugeReward = 100;
			break;
		default:
			m_enhanceGaugeReward = 20;
			break;
		}
	}

	//敵の武器参照を初期化する
	m_currentGun = nullptr;
	m_meleeWeapon = nullptr;

	//敵のランクに応じて、武器を生成し、攻撃パターンを決定する
	if (m_enemyRank == EEnemyRank::MiddleBoss || m_enemyRank == EEnemyRank::LastBoss)
	{
		//中ボスやラスボスの場合、銃と近接武器の両方を生成する
		if (m_gunClass)
		{
			//銃のスポーンパラメータを設定し、銃をスポーンする
			FActorSpawnParameters spawnParams;
			spawnParams.Owner = this;
			spawnParams.Instigator = GetInstigator();

			//銃をスポーンし、メッシュの武器ソケットにアタッチする
			m_currentGun = GetWorld()->SpawnActor<AEnemyGun>(m_gunClass, FVector::ZeroVector, FRotator::ZeroRotator, spawnParams);
			if (m_currentGun)
			{
				m_currentGun->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, TEXT("WeaponSocket"));
				m_currentGun->SetActorHiddenInGame(true);
				m_currentGun->SetActorEnableCollision(false);
			}
		}

		//近接武器のスポーンパラメータを設定し、近接武器をスポーンする
		if (m_meleeWeaponClass)
		{
			//近接武器をスポーンし、メッシュの近接武器ソケットにアタッチする
			FActorSpawnParameters spawnParams;
			spawnParams.Owner = this;
			spawnParams.Instigator = GetInstigator();

			//近接武器をスポーンし、メッシュの近接武器ソケットにアタッチする
			m_meleeWeapon = GetWorld()->SpawnActor<AActor>(m_meleeWeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, spawnParams);
			if (m_meleeWeapon)
			{
				m_meleeWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, TEXT("MeleeWeaponSocket"));
				m_meleeWeapon->SetActorHiddenInGame(true);
				m_meleeWeapon->SetActorEnableCollision(false);
			}
		}

		//初期の攻撃パターンを決定する
		DecideInitialAttackPattern();

		//溜めのエフェクトは攻撃開始時に生成し、待機中の自動再生を防ぐ
	}

	//敵のランクがミニオンの場合、現在の攻撃スタイルに応じて武器を生成する
	else
	{
		if (m_enemyRank == EEnemyRank::Minion)
		{
			if (m_currentStyle == EEnemyAttackStyle::Melee)
			{
				CreateWeapon(EEnemyAttackStyle::Melee);
				m_weaponState = EWeaponState::Ready;
				if (m_meleeWeapon)
				{
					m_meleeWeapon->SetActorHiddenInGame(false);
					m_meleeWeapon->SetActorEnableCollision(true);
				}
			}
			//銃の攻撃スタイルの場合、銃を生成する
			else if (m_currentStyle == EEnemyAttackStyle::Gun)
			{
				CreateWeapon(EEnemyAttackStyle::Gun);
				m_weaponState = EWeaponState::Ready;
				if (m_currentGun)
				{
					m_currentGun->SetActorHiddenInGame(false);
					m_currentGun->SetActorEnableCollision(false);
				}
			}
		}
		//ミニオン以外の敵の場合、銃と近接武器の両方を生成し、初期の攻撃パターンを決定する
		else
		{
			CreateWeapon(EEnemyAttackStyle::Gun);
			CreateWeapon(EEnemyAttackStyle::Melee);
			DecideInitialAttackPattern();
			m_weaponState = EWeaponState::Ready;
			SetWeaponVisibility(true);
		}
	}

	//ボスの体力UIを初期化し、ダメージ通知イベントにバインドする
	UBossEnemyWidget *healthBar = Cast<UBossEnemyWidget>(m_healthWidgetComp->GetUserWidgetObject());
	//HPのHP状態に合わせて生存または死亡処理へ分岐する
	if (healthBar)
	{
		healthBar->SetOwnerPlayer(this);
		m_onDamaged.AddDynamic(healthBar, &UBossEnemyWidget::UpdateLastBossHealthUI);
		healthBar->InitLastBossHealthUI();
		healthBar->UpdateLastBossHealthUI();
		if (m_enemyRank == EEnemyRank::LastBoss) { SetBossHealthUIVisible(!Tags.Contains(TEXT("PreloadedCombatEnemy"))); }
	}
}

//基準速度を復元する関数
void AEnemyChara::RestoreDefaultSpeed()
{
	if (GetCharacterMovement()) { GetCharacterMovement()->MaxWalkSpeed = m_defaultMoveSpeed; }
}

//FootstepAudioを最新の入力と状態へ同期する関数
void AEnemyChara::UpdateFootstepAudio()
{
	//キャラクターが移動中で、地面に接地しており、速度が一定以上の場合に足音を再生する
	if (!GetCharacterMovement() || GetCharacterMovement()->IsFalling() || GetVelocity().SizeSquared2D() < FMath::Square(45.f)) { return; }
	const float now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (now < m_nextFootstepAudioTime) { return; }

	//足音の音源を交互に切り替えて再生する
	USoundBase *stepSound = m_useAlternateFootstep ? m_enemyFootstepB.Get() : m_enemyFootstepA.Get();
	if (stepSound)
	{
		//担当機能の状態を更新するために使用するstepVolume
		const float stepVolume = m_enemyRank == EEnemyRank::Minion ? 0.48f : 0.68f;
		UGameplayStatics::PlaySoundAtLocation(this, stepSound, GetActorLocation(), stepVolume, FMath::FRandRange(0.91f, 1.02f),
			0.f, GetMutableDefault<UFootstepAttenuation>());
	}
	//次の足音再生時間を計算し、速度に応じて間隔を調整する
	m_useAlternateFootstep = !m_useAlternateFootstep;
	const float speed = GetVelocity().Size2D();
	m_nextFootstepAudioTime = now + (speed > 430.f ? 0.34f : 0.48f);
}

//攻撃対象へキャラクターの向きまたは位置を追従させる関数
void AEnemyChara::FaceTarget(const AActor *_target)
{
	//退避中は移動方向へ走り、照準要求で体だけ相手へ戻さない
	if (m_coverComponent && m_coverComponent->IsRetreatRunning()) { return; }
	//ターゲットが有効でない場合、処理を終了する
	if (!_target) { return; }

	//ターゲットの位置とキャラクターの位置から、2D平面上の方向ベクトルを計算する
	const FVector direction = (_target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	if (direction.IsNearlyZero()) { return; }

	//キャラクターの回転を、計算した方向ベクトルのYaw角度に設定する
	SetActorRotation(FRotator(0.f, direction.Rotation().Yaw, 0.f));
}

//プレイヤー 物音を周囲のAIへ通知し、索敵行動を開始させる関数
void AEnemyChara::BroadcastPlayerNoise(UObject *_worldContext, AActor *_player, const FVector &_noiseLocation, float _hearingRange)
{
	//ワールドコンテキスト、プレイヤー、聴覚範囲が有効でない場合、処理を終了する
	if (!_worldContext || !IsValid(_player) || _hearingRange <= 0.f) { return; }
	UWorld *world = _worldContext->GetWorld();
	//Worldが有効な場合だけ認識したプレイヤーに対する行動を更新する
	if (!world) { return; }

	//AIの聴覚システムに、プレイヤーの物音イベントを報告する
	UAISense_Hearing::ReportNoiseEvent(world, _noiseLocation, 1.f, _player, _hearingRange, TEXT("PlayerCombatNoise"));

	//周囲の敵キャラクターを取得し、聴覚範囲内にいる場合に索敵行動を開始させる
	TArray<AActor *> enemies;
	UGameplayStatics::GetAllActorsOfClass(world, StaticClass(), enemies);
	const float hearingRangeSq = FMath::Square(_hearingRange);

	//各敵キャラクターに対して、聴覚範囲内にいる場合に索敵行動を開始させる
	for (AActor *actor : enemies)
	{
		//敵キャラクターが有効でない場合、または体力が0以下の場合、処理をスキップする
		AEnemyChara *enemy = Cast<AEnemyChara>(actor);
		//無効な有効性を除外して残りの要素だけを処理する
		if (!IsValid(enemy) || enemy->GetHealthRatio() <= 0.f) { continue; }
		//敵との距離が行動可能範囲内か確認する
		if (FVector::DistSquared(enemy->GetActorLocation(), _noiseLocation) > hearingRangeSq) { continue; }
		//敵を制御するControllerが使用可能な場合だけ命令を送る
		if (AEnemyAIController *controller = Cast<AEnemyAIController>(enemy->GetController()))
		{
			controller->NotifyPlayerNoise(_player, _noiseLocation);
		}
	}
}

//戦闘事前生成済みを設定する関数
void AEnemyChara::SetCombatPreloaded(bool _preloaded)
{
	//敵キャラクターの表示、衝突判定、ティック処理、武器の表示を引数の内容に応じて設定する
	SetActorHiddenInGame(_preloaded);
	SetActorEnableCollision(!_preloaded);
	SetActorTickEnabled(!_preloaded);
	SetWeaponVisibility(!_preloaded);
	if (m_enemyRank == EEnemyRank::LastBoss) { SetBossHealthUIVisible(!_preloaded); }
}

//ボスHPUIVisibleを指定内容へ更新する関数
void AEnemyChara::SetBossHealthUIVisible(bool _visible)
{
	//ラスボスでない場合、または体力ウィジェットコンポーネントが存在しない場合、処理を終了する
	if (m_enemyRank != EEnemyRank::LastBoss || !m_healthWidgetComp) { return; }
	UBossEnemyWidget *healthBar = Cast<UBossEnemyWidget>(m_healthWidgetComp->GetUserWidgetObject());
	//HPが有効な場合だけ認識したプレイヤーに対する行動を更新する
	if (!healthBar) { return; }

	//引数の内容に応じて、体力UIを表示または非表示にする
	if (_visible)
	{
		if (!healthBar->IsInViewport()) { healthBar->AddToViewport(50); }
		healthBar->UpdateLastBossHealthUI();
	}
	//HPのHP状態に合わせて生存または死亡処理へ分岐する
	else if (healthBar->IsInViewport()) { healthBar->RemoveFromParent(); }
}

//敵ランクと攻撃形式から戦闘開始時の攻撃パターンを決定する関数
void AEnemyChara::DecideInitialAttackPattern()
{
	//攻撃パターンを分岐する乱数
	float random = FMath::RandRange(0, 2);
	if (random == 0)
	{
		m_currentStyle = EEnemyAttackStyle::Melee;
		m_startWithMelee = true;
	}
	else if (random == 1)
	{
		m_currentStyle = EEnemyAttackStyle::Gun;
		m_startWithMelee = false;
	}
	else
	{
		m_currentStyle = EEnemyAttackStyle::Laser;
		m_startWithMelee = false;
	}
}

//受け取った攻撃ダメージへ形態補正と無敵判定を適用し、体力と死亡状態を更新する関数
float AEnemyChara::TakeDamage(float _damageAmount, FDamageEvent const &_damageEvent, AController *_eventInstigator, AActor *_damageCauser)
{
	//吹き飛び中の倒された敵から報酬や死亡通知を重複発生させない
	if (!CanBeDamaged() || GetHealthRatio() <= 0.f) { return 0.f; }
	//親クラスのTakeDamage関数を呼び出し、ダメージ量を取得する
	float damage = Super::TakeDamage(_damageAmount, _damageEvent, _eventInstigator, _damageCauser);
	//プレイヤーへ安全にアクセスする参照
	AActor *playerSource = Cast<APlayerChara>(_damageCauser);
	if (!playerSource && _eventInstigator) { playerSource = Cast<APlayerChara>(_eventInstigator->GetPawn()); }
	//プレイヤーからのダメージであれば、周囲の敵に物音を通知する
	if (playerSource) { BroadcastPlayerNoise(this, playerSource, GetActorLocation(), 6500.f); }
	//戦闘メモリコンポーネントが存在する場合、ダメージを受けたことを通知する
	if (m_combatMemoryComponent) { m_combatMemoryComponent->NotifyDamageReceived(damage); }

	//体力コンポーネントが存在しない場合、ダメージ量をそのまま返す
	if (!m_healthComponent) { return damage; }
	const FEnemyDamageResult damageResult = m_healthComponent->ApplyDamage(damage);
	m_onDamaged.Broadcast();

	//現在のワールド時間を取得する
	const float now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (damageResult.m_died && m_enemyDeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, m_enemyDeathSound, GetActorLocation(), m_enemyRank == EEnemyRank::Minion ? 0.72f : 1.0f,
											  m_enemyRank == EEnemyRank::Minion ? 1.18f : 0.78f);
	}
	else if (m_enemyDamageSound && now >= m_nextDamageAudioTime)
	{
		UGameplayStatics::PlaySoundAtLocation(this, m_enemyDamageSound, GetActorLocation(), m_enemyRank == EEnemyRank::Minion ? 0.5f : 0.72f,
											  FMath::FRandRange(0.72f, 0.9f));
		m_nextDamageAudioTime = now + 0.16f;
	}

	//ボスのランクの場合、フェーズ変更や召喚条件をチェックする
	if (!damageResult.m_died && (m_enemyRank == EEnemyRank::LastBoss || m_enemyRank == EEnemyRank::MiddleBoss))
	{
		if (damageResult.m_phaseChanged)
		{
			if (damageResult.m_currentPhase == EBossPhase::Phase3)
			{
				m_defaultMoveSpeed *= 1.5f;
				RestoreDefaultSpeed();
			}
		}

		//召喚条件を満たしており、まだ召喚していない場合、アクション状態をIdleに設定し、ミニオンを召喚する
		if (m_enemyRank == EEnemyRank::LastBoss && damageResult.m_crossedSummonThreshold && !m_hasSummoned)
		{
			SetActionState(EActionState::Idle);
			SummonMinions();
		}
	}

	//死亡した場合、攻撃者のプレイヤーに敵撃破通知を送信し、変身ゲージ報酬を付与する
	if (damageResult.m_died)
	{
		//吹き飛ぶ遺体から攻撃や足音が続かないよう、予約済みの行動を停止する
		RecoverFromStalledAction();
		GetWorldTimerManager().ClearAllTimersForObject(this);
		SetActorTickEnabled(false);
		//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
		if (APlayerChara *killer = Cast<APlayerChara>(playerSource); killer && killer->m_playerUi) { killer->m_playerUi->NotifyEnemyKilled(); }
		AwardEnhanceGauge(_eventInstigator, _damageCauser);

		//ラスボスの場合、体力UIを非表示にする
		if (m_enemyRank == EEnemyRank::LastBoss)
		{
			UBossEnemyWidget *healthBar = Cast<UBossEnemyWidget>(m_healthWidgetComp->GetUserWidgetObject());
			//HPのHP状態に合わせて生存または死亡処理へ分岐する
			if (healthBar) { healthBar->RemoveFromParent(); }
		}

		//死亡時のアニメーションを停止し、アニメーションモードをブループリントに戻す
		if (GetMesh() && GetMesh()->GetAnimInstance())
		{
			GetMesh()->GetAnimInstance()->StopAllMontages(0.f);
			GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		}

		//死亡時の武器やエフェクトを破棄する
		if (IsValid(m_currentGun))
		{
			m_currentGun->Destroy();
			m_currentGun = nullptr;
		}

		//死亡時の近接武器を破棄する
		if (IsValid(m_meleeWeapon))
		{
			m_meleeWeapon->Destroy();
			m_meleeWeapon = nullptr;
		}

		//キック撃破だけ短時間体を残し、即時削除で吹き飛びが見えなくならないようにする
		APlayerChara *killer = Cast<APlayerChara>(_damageCauser);
		if (killer && killer->IsWerewolf() && killer->m_currentComboIndex == 2 && killer->m_combatComponent &&
			killer->m_combatComponent->m_meleeAttackHitBox)
		{
			if (AAIController *controller = Cast<AAIController>(GetController())) { controller->StopMovement(); controller->UnPossess(); }
			GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Ignore);
			GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			GetMesh()->bPauseAnims = true;
			m_healthWidgetComp->SetVisibility(false);
			GetCharacterMovement()->bRunPhysicsWithNoController = true;
			LaunchCharacter(killer->m_combatComponent->m_meleeAttackHitBox->GetKickVelocity(this), true, true);
			SetLifeSpan(0.75f);
		}
		else { Destroy(); }
	}

	//中ボスが退却すべき場合、退却処理を実行する
	if (damageResult.m_shouldRetreat)
	{
		m_onMiddleBossRetreat.Broadcast();
		PerformRetreat();
	}

	return damage;
}

//戦闘が現在成立しているかを判定する関数
bool AEnemyChara::IsInCombat() const
{
	return m_combatMemoryComponent && m_combatMemoryComponent->IsInCombat();
}

//戦闘Postureの現在値を参照側へ渡す関数
EEnemyCombatPosture AEnemyChara::GetCombatPosture() const
{
	return m_combatMemoryComponent ? m_combatMemoryComponent->GetCombatPosture() : EEnemyCombatPosture::Relaxed;
}

//戦闘待機アニメーションを更新する関数
void AEnemyChara::UpdateCombatIdleAnimation()
{
	//敵のランクがミニオンの場合、またはメッシュや戦闘待機アニメーションが存在しない場合、戦闘待機アニメーションを停止する
	if (m_enemyRank == EEnemyRank::Minion || !GetMesh() || !m_combatIdleAnimation)
	{
		StopCombatIdleAnimation();
		return;
	}

	//戦闘が成立しており、アクション状態がIdleで、武器切り替えやテレポート中でなく、速度が一定以下の場合に戦闘待機アニメーションを再生する
	const bool shouldPlayCombatIdle = IsInCombat() && m_actionState == EActionState::Idle && !m_isSwitchingWeapon && !m_isTeleporting &&
										GetVelocity().SizeSquared2D() <= FMath::Square(5.f) && !GetCharacterMovement()->IsFalling();
	//対象が有効な場合だけ認識したプレイヤーに対する行動を更新する
	if (!shouldPlayCombatIdle)
	{
		StopCombatIdleAnimation();
		return;
	}

	//戦闘待機アニメーションが既に再生中の場合、処理を終了する
	if (m_playingCombatIdle) { return; }

	//AnimBPの移動状態を破棄せず、待機姿勢だけを重ねる
	UAnimInstance *anim = GetMesh()->GetAnimInstance();
	if (!anim) { return; }
	m_idleMontage = anim->PlaySlotAnimationAsDynamicMontage(m_combatIdleAnimation, TEXT("DefaultSlot"), 0.15f, 0.15f, 1.f, 10000);
	m_playingCombatIdle = IsValid(m_idleMontage);
}

//戦闘 待機 アニメーションを終了し、専用タイマーと一時フラグを解除する関数
void AEnemyChara::StopCombatIdleAnimation()
{
	//戦闘待機アニメーションが再生中でない場合、またはメッシュが存在しない場合、処理を終了する
	if (!m_playingCombatIdle || !GetMesh()) { return; }

	//待機だけを終了し、歩行や攻撃の再生状態を初期化しない
	m_playingCombatIdle = false;
	if (UAnimInstance *anim = GetMesh()->GetAnimInstance(); anim && m_idleMontage) { anim->Montage_Stop(0.15f, m_idleMontage); }
	m_idleMontage = nullptr;
}

//敵撃破を確定した攻撃者のプレイヤーへ変身ゲージ報酬を一度だけ付与する関数
void AEnemyChara::AwardEnhanceGauge(AController *_eventInstigator, AActor *_damageCauser)
{
	//既に変身ゲージ報酬が付与されている場合、処理を終了する
	if (m_enhanceRewardGranted) { return; }

	//攻撃者のプレイヤーキャラクターを取得するために、ダメージ原因やイベント発生者からキャストを試みる
	APlayerChara *player = Cast<APlayerChara>(_damageCauser);
	if (!player && _damageCauser) { player = Cast<APlayerChara>(_damageCauser->GetOwner()); }
	if (!player && _eventInstigator) { player = Cast<APlayerChara>(_eventInstigator->GetPawn()); }
	if (!player && _damageCauser && _damageCauser->GetInstigatorController())
	{
		player = Cast<APlayerChara>(_damageCauser->GetInstigatorController()->GetPawn());
	}
	//プレイヤーが有効な場合だけ認識したプレイヤーに対する行動を更新する
	if (!player) { return; }

	m_enhanceRewardGranted = true;
	player->AddEnhanceGauge(m_enhanceGaugeReward);
}

//退避を実行する関数
void AEnemyChara::PerformRetreat()
{
	//非表示になる前に跳躍・レーザーの遅延処理を中断する
	RecoverFromStalledAction();
	//アクション状態をIdleに設定し、経過時間をリセットする
	CloseMeleeHitWindow();
	GetWorldTimerManager().ClearTimer(m_reloadTimerHandle);

	//敵キャラクターのアクション状態をIdleに設定し、経過時間をリセットする
	if (GetMesh() && GetMesh()->GetAnimInstance()) { GetMesh()->GetAnimInstance()->StopAllMontages(0.1f); }

	//敵キャラクターのアクション状態をIdleに設定し、経過時間をリセットする
	m_isSwitchingWeapon = false;
	m_isTeleporting = false;

	//敵キャラクターのアクション状態をIdleに設定し、経過時間をリセットする
	GetWorldTimerManager().ClearTimer(m_teleportTimerHandle);
	DestroyAllTransientBossEffects();

	//敵キャラクターを非表示にし、衝突判定とティック処理を無効化する
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);

	//キャラクターの移動を無効化する
	if (GetCharacterMovement()) { GetCharacterMovement()->DisableMovement(); }

	//銃が有効な場合、非表示にして衝突判定を無効化し、射撃を停止する
	if (IsValid(m_currentGun))
	{
		m_currentGun->SetActorHiddenInGame(true);
		m_currentGun->SetActorEnableCollision(false);
		m_currentGun->StopFire();
	}

	//近接武器が有効な場合、非表示にして衝突判定を無効化し、武器を非アクティブ化する
	if (IsValid(m_meleeWeapon))
	{
		m_meleeWeapon->SetActorHiddenInGame(true);
		m_meleeWeapon->SetActorEnableCollision(false);
		//Actorへ安全にアクセスする参照
		AMeleeWeapon *meleeActor = Cast<AMeleeWeapon>(m_meleeWeapon);
		if (meleeActor) { meleeActor->DeactivateWeapon(); }
	}
}

//行動状態を設定する関数
void AEnemyChara::SetActionState(EActionState _newState)
{
	//モンタージュ終了通知で跳躍途中の攻撃を解除しない
	if (m_smashPending && _newState == EActionState::Idle && GetHealthRatio() > 0.f) { return; }
	//遅れて届いたアニメーション通知で被弾硬直を解除しない
	if (IsKnockedBack() && _newState != EActionState::Stunned && GetHealthRatio() > 0.f) { return; }
	if (_newState != EActionState::Idle) { StopCombatIdleAnimation(); }
	m_actionState = _newState;
	m_actionStateElapsed = 0.f;
	if (m_stateComponent) { m_stateComponent->SetActionState(_newState); }
}

//リロード状態を設定する関数
void AEnemyChara::SetReloadState(bool _isReloading)
{
	m_isReloading = _isReloading;
	if (m_stateComponent) { m_stateComponent->SetReloading(_isReloading); }
}

//退避が必要か判定する関数
bool AEnemyChara::ShouldRetreat() const
{
	return m_healthComponent && m_healthComponent->ShouldRetreat();
}

//HP割合を取得する関数
float AEnemyChara::GetHealthRatio() const
{
	return m_healthComponent ? m_healthComponent->GetHealthRatio() : 0.f;
}

//現在のHPを取得する関数
float AEnemyChara::GetCurrentHealth() const
{
	return m_healthComponent ? m_healthComponent->GetCurrentHealth() : 0.f;
}

//最大HPを取得する関数
float AEnemyChara::GetMaxHealth() const
{
	return m_healthComponent ? m_healthComponent->GetMaxHealth() : 0.f;
}

//可能退避を設定する関数
void AEnemyChara::SetCanRetreat(bool _canRetreat)
{
	m_canRetreat = _canRetreat;
	//HPのHP状態に合わせて生存または死亡処理へ分岐する
	if (m_healthComponent) { m_healthComponent->SetCanRetreat(_canRetreat); }
}

//ボスフェーズを取得する関数
EBossPhase AEnemyChara::GetBossPhase() const
{
	return m_healthComponent ? m_healthComponent->GetBossPhase() : EBossPhase::Phase1;
}
