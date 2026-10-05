#include "Weapons/EnemyBullet.h"

#include "Components/StaticMeshComponent.h"
#include "Combat/CombatTracer.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Sound/SoundBase.h"
#include "Player/PlayerChara.h"
#include "Enemy/EnemyChara.h"
#include "Enemy/Components/EnemyCombatMemoryComponent.h"
#include "EngineUtils.h"
#include "Camera/CameraComponent.h"

//敵弾の飛翔、衝突、プレイヤーダメージを管理する弾丸の既定値とコンポーネント構成を初期化する関数
AEnemyBullet::AEnemyBullet() : m_damage(1.f)
{
	//敵弾はProjectileMovementで移動するためActorのTickを無効化する
	PrimaryActorTick.bCanEverTick = false;
	m_collisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("ProjectileCollision"));
	SetRootComponent(m_collisionSphere);
	m_tracer = CreateDefaultSubobject<UCombatTracer>(TEXT("CombatTracer"));
	m_tracer->SetupAttachment(m_collisionSphere);
	m_collisionSphere->InitSphereRadius(16.f);
	m_collisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	m_collisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	m_collisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	m_collisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	m_collisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	m_collisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	m_collisionSphere->SetNotifyRigidBodyCollision(true);
	m_collisionSphere->BodyInstance.bUseCCD = true;
	m_collisionSphere->OnComponentHit.AddDynamic(this, &AEnemyBullet::OnHit);

	m_bulletMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BulletMesh"));
	m_bulletMesh->SetupAttachment(m_collisionSphere);
	m_bulletMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	m_projectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	m_projectileMovement->SetUpdatedComponent(m_collisionSphere);
	m_projectileMovement->InitialSpeed = 7000.f;
	m_projectileMovement->MaxSpeed = 8000.f;
	m_projectileMovement->bRotationFollowsVelocity = true;
	m_projectileMovement->bShouldBounce = false;
	m_projectileMovement->ProjectileGravityScale = 0.f;

	m_beamTrail = CreateDefaultSubobject<UNiagaraComponent>(TEXT("BeamTrail"));
	m_beamTrail->SetupAttachment(m_bulletMesh);
	m_beamTrail->SetAutoActivate(false);
	m_beamTrail->SetAutoDestroy(false);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> beamTrailFinder(
		TEXT("/Game/MixedVFX/Particles/Projectiles/NS_Projectile_05.NS_Projectile_05"));
	//軌跡Niagaraを読み込めた場合は白い仮Meshを隠してBeam表示へ切り替える
	if (beamTrailFinder.Succeeded())
	{
		m_beamTrail->SetAsset(beamTrailFinder.Object);
		m_beamTrail->SetRelativeScale3D(FVector(0.2f));
		m_bulletMesh->SetVisibility(false, true);
	}
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> impactFinder(
		TEXT("/Game/MixedVFX/Particles/Projectiles/Hits/NS_Projectile_05_Hit.NS_Projectile_05_Hit"));
	m_impactEffect = impactFinder.Object;
	//敵弾の最大射程に必要な時間だけ寿命を確保して不要な弾道を早く消す処理
	InitialLifeSpan = 0.65f;
}

//ゲーム開始時に必要な参照を取得し、初期状態とイベント通知を設定する関数
void AEnemyBullet::BeginPlay()
{
	//基底Actorのゲーム開始処理を実行する
	Super::BeginPlay();

	m_damage = 1.f;
	//BlueprintでComponent参照が失われた場合はActor内からCollisionを再取得する
	if (!m_collisionSphere) m_collisionSphere = FindComponentByClass<USphereComponent>();
	//BlueprintでComponent参照が失われた場合はActor内からMeshを再取得する
	if (!m_bulletMesh) m_bulletMesh = FindComponentByClass<UStaticMeshComponent>();
	//BlueprintでComponent参照が失われた場合はActor内から移動Componentを再取得する
	if (!m_projectileMovement) m_projectileMovement = FindComponentByClass<UProjectileMovementComponent>();
	//必須Componentが一つでも欠ける場合は不完全な敵弾を破棄する
	if (!m_collisionSphere || !m_bulletMesh || !m_projectileMovement)
	{
		Destroy();
		return;
	}
	//敵弾がプレイヤーを確実にSweep判定できる衝突設定をゲーム開始時に再適用する処理
	if (m_beamTrail) { m_beamTrail->DeactivateImmediate(); m_beamTrail->SetVisibility(false); }
	m_bulletMesh->SetVisibility(false);
	if (m_tracer) { m_tracer->SetEnemyColor(true); }
	m_collisionSphere->SetSphereRadius(16.f);
	m_collisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	m_collisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	m_collisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	m_collisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	m_collisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	m_collisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	m_collisionSphere->SetNotifyRigidBodyCollision(true);
	m_collisionSphere->BodyInstance.bUseCCD = true;
	m_bulletMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	m_projectileMovement->SetUpdatedComponent(m_collisionSphere);
	//発射した敵自身との衝突を無視する
	if (GetOwner()) m_collisionSphere->MoveIgnoreActors.Add(GetOwner());
	//発射元として設定されたPawnとの衝突を無視する
	if (GetInstigator()) m_collisionSphere->MoveIgnoreActors.Add(GetInstigator());
	//World内の敵を順番に除外して敵弾同士の誤爆を防ぐ
	for (TActorIterator<AEnemyChara> enemyIt(GetWorld()); enemyIt; ++enemyIt)
	{
		//有効な敵だけを移動Collisionの無視対象へ加える
		if (!IsValid(*enemyIt)) { continue; }
		m_collisionSphere->IgnoreActorWhenMoving(*enemyIt, true);
		//敵自身が移動する際のSweepからも弾を除外し、味方の銃撃で足止めされることを防ぐ
		if (UCapsuleComponent *body = enemyIt->GetCapsuleComponent())
		{
			body->IgnoreActorWhenMoving(this, true);
			m_ignoredBodies.Add(body);
		}
	}
}

//着弾や寿命切れの後に不要な衝突除外を残さない関数
void AEnemyBullet::EndPlay(const EEndPlayReason::Type _reason)
{
	for (const TWeakObjectPtr<UPrimitiveComponent> &body : m_ignoredBodies)
	{
		if (body.IsValid()) { body->IgnoreActorWhenMoving(this, false); }
	}
	m_ignoredBodies.Reset();
	Super::EndPlay(_reason);
}

//敵弾の命中対象へDamageと縮小した命中Effectを一度だけ適用する関数
void AEnemyBullet::OnHit(UPrimitiveComponent *_hitComponent, AActor *_otherActor, UPrimitiveComponent *_otherComponent, FVector _normalImpulse,
						 const FHitResult &_hit)
{
	//同じ衝突からDamageとEffectを複数回発生させない
	if (m_hasHit) { return; }
	//無効なActor、敵弾自身、発射元への命中を処理しない
	if (!_otherActor || _otherActor == this || _otherActor == GetOwner()) { return; }
	m_hasHit = true;
	//命中後の追加Collisionを防ぐためSphere判定を無効化する
	if (m_collisionSphere) m_collisionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	//命中後にMeshが他の物体へ衝突しないようCollisionを無効化する
	if (m_bulletMesh) m_bulletMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	//敵Rankに応じたDamageを取得する発射元の敵
	const AEnemyChara *sourceEnemy = Cast<AEnemyChara>(GetOwner());
	//Ownerから取得できない場合はInstigatorから発射元を取得する
	if (!sourceEnemy) sourceEnemy = Cast<AEnemyChara>(GetInstigator());
	//発射元の敵または敵弾既定値から決める適用予定Damage
	const float requestedDamage = sourceEnemy ? sourceEnemy->GetProjectileAttackDamage() : m_damage;
	//ApplyDamageがプレイヤーへ実際に適用したDamage
	float appliedDamage = 0.f;
	//命中Actorがプレイヤーか確認するCast結果
	APlayerChara *hitPlayer = Cast<APlayerChara>(_otherActor);
	//プレイヤーへ命中した場合だけHPを減らす
	if (hitPlayer)
	{
		appliedDamage = UGameplayStatics::ApplyDamage(hitPlayer, requestedDamage, GetInstigatorController(), this, UDamageType::StaticClass());
		if (appliedDamage > 0.f && sourceEnemy && sourceEnemy->m_combatMemoryComponent)
		{
			sourceEnemy->m_combatMemoryComponent->RecordShotHit();
		}
	}
	//プレイヤーへの命中は被弾UIに任せ、カメラを覆う粒子を生成しない
	if (!hitPlayer)
	{
		const FVector normal = _hit.ImpactNormal.IsNearlyZero() ? -GetActorForwardVector() : _hit.ImpactNormal.GetSafeNormal();
		UCombatTracer::ShowImpact(GetWorld(), _hit.ImpactPoint, normal, true);
	}
	//近未来型の命中音を読み込めた場合は接触位置から再生する
	if (USoundBase *impactSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_BeamImpact.SFX_BeamImpact")))
	{
		UGameplayStatics::PlaySoundAtLocation(this, impactSound, GetActorLocation(), 0.72f, FMath::FRandRange(0.92f, 1.02f));
	}
	Destroy();
}
