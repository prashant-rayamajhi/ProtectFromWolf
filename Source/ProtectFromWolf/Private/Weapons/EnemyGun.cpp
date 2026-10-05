#include "Weapons/EnemyGun.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Weapons/EnemyBullet.h"
#include "Enemy/EnemyChara.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Particles/ParticleSystemComponent.h"
#include "Sound/SoundBase.h"
#include "AIController.h"
#include "AI/Controllers/EnemyAIController.h"
#include "Enemy/Components/EnemyCombatMemoryComponent.h"

//コンストラクタによる初期化処理を行う関数
AEnemyGun::AEnemyGun()
{
	//射撃はTimerから実行するためActorのTickを無効化する
	PrimaryActorTick.bCanEverTick = false;

	//各コンポーネントの作成と階層設定
	m_sceneComp = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(m_sceneComp);

	m_meshComp = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	m_meshComp->SetupAttachment(m_sceneComp);

	m_muzzle = CreateDefaultSubobject<USceneComponent>(TEXT("MuzzleSpawn"));
	m_muzzle->SetupAttachment(m_meshComp);

	//ステータスの初期値を設定
	m_fireRate = 0.14f;
	m_fireRange = 3000.f;
	m_clipSize = 30;
	m_totalAmmo = 150;
	m_reloadTime = 2.0f;
	m_maxBurstShots = 3;
	m_burstCooldown = 0.9f;
	m_shotsInBurst = 0;
	static ConstructorHelpers::FClassFinder<AEnemyBullet> bulletClass(TEXT("/Game/Enemy/BP_Enemy/BP_EnemyBullet"));
	//敵弾Blueprintを読み込めた場合は射撃時の生成Classへ設定する
	if (bulletClass.Succeeded()) m_bulletClass = bulletClass.Class;
	//近未来型MuzzleFlashとして使用するNiagara Assetの検索結果
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> muzzleFinder(
		TEXT("/Game/MixedVFX/Particles/Projectiles/NS_Projectile_05.NS_Projectile_05"));
	//Niagara Assetを読み込めた場合はMuzzleFlashへ設定する
	if (muzzleFinder.Succeeded()) m_enemyMuzzlePulseNiagara = muzzleFinder.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> fireSoundFinder(TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_EnemyFire.SFX_EnemyFire"));
	//射撃音を読み込めた場合は標準の敵銃声へ設定する
	if (fireSoundFinder.Succeeded()) m_muzzleSound = fireSoundFinder.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> minionPulseFinder(TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_BeamImpact.SFX_BeamImpact"));
	//雑魚敵用Pulse音を読み込めた場合は専用銃声へ設定する
	if (minionPulseFinder.Succeeded()) m_minionPulseSound = minionPulseFinder.Object;
}

//ゲーム開始時の関数
void AEnemyGun::BeginPlay()
{
	//基底Actorのゲーム開始処理を実行する
	Super::BeginPlay();
	//銃Meshが発射直後の敵弾を遮らないようCollisionを無効化する
	if (m_meshComp) m_meshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	//古い銃声を置き換える近未来型射撃音
	USoundBase *sciFiFireSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_EnemyFire.SFX_EnemyFire"));
	//近未来型射撃音を読み込めた場合は敵の銃声へ反映する
	if (sciFiFireSound)
	{
		//古い通常銃声を近未来のパルス音へ置き換える処理
		if (!m_muzzleSound || m_muzzleSound->GetPathName().Contains(TEXT("/Assets/Sound/"))) { m_muzzleSound = sciFiFireSound; }
		m_minionPulseSound = sciFiFireSound;
	}
	//Constructorで取得できなかった場合はゲーム開始時にNiagaraを再読込する
	if (!m_enemyMuzzlePulseNiagara)
	{
		m_enemyMuzzlePulseNiagara =
			LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/MixedVFX/Particles/Projectiles/NS_Projectile_05.NS_Projectile_05"));
	}

	m_canFire = true;
	m_currentAmmo = m_clipSize;

	m_fireRate = 0.14f;
	m_maxBurstShots = 3;
	m_burstCooldown = 0.9f;
}

//連射を開始する関数
void AEnemyGun::StartFire()
{
	//アクターが破壊されている場合は処理を中断する
	if (!m_canFire || !IsValid(this)) { return; }
	//既に連射Timerが動いている場合は重複登録しない
	if (UWorld *world = GetWorld())
	{
		//同じ銃の連射Timerが有効な場合は開始要求を終了する
		if (world->GetTimerManager().IsTimerActive(m_autoFireTimer)) { return; }
	}

	//マガジンに弾が残っている場合は一発目を即座に発射する
	if (m_currentAmmo > 0)
	{
		m_shotsInBurst = 0;
		FireShot();
		//一発目でBurst上限へ達した場合は連射Timerを登録しない
		if (m_shotsInBurst >= m_maxBurstShots) { return; }
		//Ownerが攻撃状態を終了している場合は次弾を発射しない
		if (const AEnemyChara *ownerEnemy = Cast<AEnemyChara>(GetOwner()); !ownerEnemy || !ownerEnemy->IsAttacking()) { return; }

		//安全にタイマーを登録するためワールドの存在を確認する
		if (UWorld *world = GetWorld()) { world->GetTimerManager().SetTimer(m_autoFireTimer, this, &AEnemyGun::FireShot, m_fireRate, true); }
	}
	else { StopFire(); }
}

//連射を停止する関数
void AEnemyGun::StopFire()
{
	//安全にタイマーを解除するためワールドの存在を確認する
	if (UWorld *world = GetWorld()) { world->GetTimerManager().ClearTimer(m_autoFireTimer); }
}

//弾を補充する処理を行う関数
void AEnemyGun::ReloadAmmo()
{
	//Reload中に残った連射Timerを停止する
	StopFire();

	//予備弾薬がない場合またはマガジンが満タンの場合は補充しない
	if (m_totalAmmo <= 0 || m_currentAmmo >= m_clipSize) { return; }

	//マガジンを満たすために必要な弾数
	int32 ammoNeeded = m_clipSize - m_currentAmmo;
	//予備弾薬から実際に移動できる弾数
	int32 ammoToLoad = FMath::Min(ammoNeeded, m_totalAmmo);

	m_totalAmmo -= ammoToLoad;
	m_currentAmmo += ammoToLoad;

	m_canFire = true;
}

//弾を生成して発射する処理を行う関数
void AEnemyGun::FireShot()
{
	//アクターが破壊されている場合は処理を中断する
	if (!IsValid(this)) { return; }

	//呼び出し元に関係なく一回のバーストを三発へ制限する処理
	if (!m_canFire || m_shotsInBurst >= m_maxBurstShots)
	{
		StopFire();
		return;
	}

	//マガジンが空の場合はBurstを終了する
	if (m_currentAmmo <= 0)
	{
		StopFire();
		return;
	}

	//射撃状態と対象方向を管理する銃の所有者
	AEnemyChara *ownerEnemy = Cast<AEnemyChara>(GetOwner());
	//毎弾の発射時点で更新するプレイヤー対象
	AActor *target = GetWorld() ? UGameplayStatics::GetPlayerPawn(GetWorld(), 0) : nullptr;
	//対象への射線を確認する所有者のAI Controller
	const AEnemyAIController *aiController = ownerEnemy ? Cast<AEnemyAIController>(ownerEnemy->GetController()) : nullptr;
	//発射時点でプレイヤーまで射線が通っているか示す状態
	const bool hasFireLine = aiController && aiController->CanObserveTarget(target);
	//所有者、対象、射線のいずれかが無効な場合はBurstを中断する
	if (!ownerEnemy || ownerEnemy->GetHealthRatio() <= 0.f || !ownerEnemy->IsAttacking() || !IsValid(target) || !hasFireLine)
	{
		//所有者が有効なら敵側の攻撃Animationも含めて停止する
		if (ownerEnemy)
			ownerEnemy->StopFiring();
		else
			StopFire();
		return;
	}
	ownerEnemy->FaceTarget(target);
	//二発目以降も停止通知や移動による射撃姿勢の解除を確認する
	if (!ownerEnemy->IsInGunFireWindow()) { StopFire(); return; }

	//敵弾Classが設定されている場合だけ発射位置と予測照準を計算する
	if (m_bulletClass)
	{
		//敵弾とMuzzleFlashを生成する銃口位置
		FVector socketLocation = FVector::ZeroVector;
		//プレイヤーへ向けて補正する前の銃口回転
		FRotator socketRotation = FRotator::ZeroRotator;

		//専用Socketがある場合は正確な銃口位置と回転を使用する
		if (m_meshComp && m_meshComp->DoesSocketExist(TEXT("MuzzleFlashSocket")))
		{
			socketLocation = m_meshComp->GetSocketLocation(TEXT("MuzzleFlashSocket"));
			socketRotation = m_meshComp->GetSocketRotation(TEXT("MuzzleFlashSocket"));
		}
		//専用Socketがない場合は銃Mesh前方を代替銃口として使用する
		else if (m_meshComp)
		{
			socketLocation = m_meshComp->GetComponentLocation() + (m_meshComp->GetForwardVector() * 50.f);
			socketRotation = m_meshComp->GetComponentRotation();
		}

		//敵弾が足元へ向かわないように高さを加えたプレイヤー中心
		const FVector targetCenter = target->GetActorLocation() + FVector(0.f, 0.f, 60.f);
		//敵弾が到達するまでにプレイヤーが移動する予測時間
		const float travelTime = FMath::Min(FVector::Distance(socketLocation, targetCenter) / 7000.f, 0.35f);
		//現在速度から予測した敵弾到達時のプレイヤー位置
		const FVector predictedTarget = targetCenter + target->GetVelocity() * travelTime;
		//銃口から予測位置へ向かう正規化済み発射方向
		const FVector shotDirection = (predictedTarget - socketLocation).GetSafeNormal();
		socketRotation = shotDirection.Rotation();
		//銃本体との重なりを防ぐため銃口から前へ出した敵弾生成位置
		FVector spawnLocation = socketLocation + (shotDirection * 5.f);
		//目から見えていても銃口が壁や味方に塞がれている場合は発射しない
		FCollisionQueryParams query(SCENE_QUERY_STAT(EnemyMuzzleClearance), false);
		query.AddIgnoredActor(this);
		query.AddIgnoredActor(ownerEnemy);
		FHitResult obstruction;
		if (GetWorld()->LineTraceSingleByChannel(obstruction, socketLocation, predictedTarget, ECC_Visibility, query) &&
			obstruction.GetActor() != target)
		{
			ownerEnemy->StopFiring();
			return;
		}

		//敵弾へDamage元となるOwnerとInstigatorを渡す生成Parameter
		FActorSpawnParameters spawnParams;
		spawnParams.Owner = GetOwner();
		spawnParams.Instigator = GetInstigator();

		//安全に弾を生成するためワールドの存在を確認する
		if (UWorld *world = GetWorld())
		{
			//銃口からプレイヤーの予測位置へ生成した敵弾
			AEnemyBullet *spawnedBullet = world->SpawnActor<AEnemyBullet>(m_bulletClass, spawnLocation, socketRotation, spawnParams);
			if (!spawnedBullet) { ownerEnemy->StopFiring(); return; }
			--m_currentAmmo;
			if (ownerEnemy->m_combatMemoryComponent) { ownerEnemy->m_combatMemoryComponent->RecordShot(); }
		}

		//エフェクトの再生処理
		//近未来型Niagaraが設定されている場合は小さなMuzzleFlashを銃口へ生成する
		if (m_enemyMuzzlePulseNiagara)
		{
			//銃口Socketへ追従させるNiagara MuzzleFlash
			UNiagaraComponent *muzzleEffect =
				UNiagaraFunctionLibrary::SpawnSystemAttached(m_enemyMuzzlePulseNiagara, m_meshComp, TEXT("MuzzleFlashSocket"), FVector::ZeroVector,
															 FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true);
			//生成成功時にScaleと寿命を制限して視界を塞がないようにする
			if (muzzleEffect)
			{
				muzzleEffect->SetWorldScale3D(FVector(0.03f));
				muzzleEffect->SetAutoDestroy(true);
				//MuzzleFlashを短時間で停止するTimer
				FTimerHandle cleanupTimer;
				//Niagaraを即時停止するTimer処理
				FTimerDelegate cleanupDelegate;
				cleanupDelegate.BindUObject(muzzleEffect, &UNiagaraComponent::DeactivateImmediate);
				GetWorld()->GetTimerManager().SetTimer(cleanupTimer, cleanupDelegate, 0.12f, false);
			}
		}
		//Niagaraがない場合は既存Particleを代替MuzzleFlashとして生成する
		else if (m_muzzleFlash)
		{
			//銃口Socketへ追従させるParticle MuzzleFlash
			UParticleSystemComponent *muzzleEffect = UGameplayStatics::SpawnEmitterAttached(m_muzzleFlash, m_meshComp, TEXT("MuzzleFlashSocket"));
			//生成成功時に寿命を制限して残留Effectを防ぐ
			if (muzzleEffect)
			{
				muzzleEffect->bAutoDestroy = true;
				//MuzzleFlashを短時間で停止するTimer
				FTimerHandle cleanupTimer;
				//Particleを停止するTimer処理
				FTimerDelegate cleanupDelegate;
				cleanupDelegate.BindUObject(muzzleEffect, &UParticleSystemComponent::DeactivateSystem);
				GetWorld()->GetTimerManager().SetTimer(cleanupTimer, cleanupDelegate, 0.12f, false);
			}
		}

		//敵Rankに応じて切り替える発射音
		USoundBase *shotSound = m_muzzleSound;
		//標準発射音の再生音量
		float shotVolume = 0.92f;
		//連続射撃を単調にしないための標準Pitch
		float shotPitch = FMath::FRandRange(0.98f, 1.03f);
		//雑魚敵は高いPitchのPulse音へ切り替えてボスの攻撃音と区別する
		if (ownerEnemy->m_enemyRank == EEnemyRank::Minion && m_minionPulseSound)
		{
			shotSound = m_minionPulseSound;
			shotVolume = 0.82f;
			shotPitch = FMath::FRandRange(1.08f, 1.2f);
		}
		//発射音が有効な場合は銃口位置から再生する
		if (shotSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, shotSound, socketLocation, shotVolume, shotPitch);
		}
	}
	else
	{
		ownerEnemy->StopFiring();
		return;
	}

	++m_shotsInBurst;
	//規定弾数へ達したらBurstを終了してCooldownを開始する
	if (m_shotsInBurst >= m_maxBurstShots)
	{
		m_canFire = false;
		//Worldが有効な場合は次のBurstを許可するTimerを設定する
		if (UWorld *world = GetWorld())
		{
			world->GetTimerManager().SetTimer(m_burstCooldownTimer, this, &AEnemyGun::ResetFire, m_burstCooldown, false);
		}
		//射撃間隔だけを空け、銃を構えるアニメーションと戦闘状態は維持する
		StopFire();
	}
}

//射撃可能な状態にリセットする処理を行う関数
void AEnemyGun::ResetFire()
{
	//次のBurstを開始できるように発射数と射撃許可を初期化する
	m_shotsInBurst = 0;
	m_canFire = true;
	//移動や被弾で射撃を中断していなければ、構えを保ったまま次のバーストへ移る
	AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner());
	if (enemy && enemy->GetHealthRatio() > 0.f && enemy->IsAttacking() && enemy->m_currentStyle == EEnemyAttackStyle::Gun)
	{
		enemy->BeginFireSequence();
	}
}

//弾を消費する処理を行う関数
bool AEnemyGun::ConsumeAmmo()
{
	//マガジンに弾が残っている場合だけ一発消費する
	if (m_currentAmmo > 0)
	{
		m_currentAmmo--;
		return true;
	}
	return false;
}
