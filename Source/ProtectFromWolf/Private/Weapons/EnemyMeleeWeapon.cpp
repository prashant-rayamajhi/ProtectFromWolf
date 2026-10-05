#include "Weapons/EnemyMeleeWeapon.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/DamageType.h"
#include "Components/BoxComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Player/PlayerChara.h"

//コンストラクタによる初期化処理を行う関数
AMeleeWeapon::AMeleeWeapon() : m_damage(5.0f), m_hasHit(false)
{
	PrimaryActorTick.bCanEverTick = false;

	m_weaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	RootComponent = m_weaponMesh;
	m_weaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	m_collisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	m_collisionBox->SetupAttachment(m_weaponMesh);

	m_collisionBox->SetBoxExtent(FVector(20.f, 20.f, 70.f));

	m_collisionBox->BodyInstance.bUseCCD = true;

	m_collisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	m_collisionBox->SetCollisionObjectType(ECC_WorldDynamic);
	m_collisionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	m_collisionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	m_collisionBox->OnComponentBeginOverlap.AddDynamic(this, &AMeleeWeapon::OnWeaponOverlap);
}

//ゲーム開始時の処理を行う関数
void AMeleeWeapon::BeginPlay()
{
	Super::BeginPlay();
	DeactivateWeapon();
}

//攻撃モーションに合わせて当たり判定を有効にする処理を行う関数
void AMeleeWeapon::ActivateWeapon()
{
	m_hitActors.Empty();
	m_hasHit = false;

	if (m_collisionBox) { m_collisionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly); }
}

//攻撃モーション終了に合わせて当たり判定を無効にする処理を行う関数
void AMeleeWeapon::DeactivateWeapon()
{
	if (m_collisionBox) { m_collisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision); }

	m_hitActors.Empty();
	m_hasHit = false;
}

//プレイヤーへ一振りで与えるダメージ量を変更する関数
void AMeleeWeapon::SetDamage(float _damage)
{
	m_damage = _damage;
}

//剣へ重なったプレイヤーを検証し、一振り一回だけダメージを適用する関数
void AMeleeWeapon::OnWeaponOverlap(UPrimitiveComponent *_overlappedComp, AActor *_otherActor, UPrimitiveComponent *_otherComp, int32 _otherBodyIndex,
								   bool _bFromSweep, const FHitResult &_sweepResult)
{
	if (!_otherActor || _otherActor == this || _otherActor == GetOwner()) { return; }

	if (m_hitActors.Contains(_otherActor)) { return; }

	bool bIsPlayer = _otherActor->ActorHasTag("Player") || _otherActor->IsA(APlayerChara::StaticClass());

	//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
	if (bIsPlayer)
	{
		m_hasHit = true;
		m_hitActors.Add(_otherActor);

		UGameplayStatics::ApplyDamage(_otherActor, m_damage, GetInstigatorController(), this, UDamageType::StaticClass());
	}
}
