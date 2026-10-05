#include "Combat/FireComponent.h"
#include "Weapons/EnemyBullet.h"
#include "TimerManager.h"
#include "Enemy/EnemyChara.h"
#include "Kismet/GameplayStatics.h"

//敵弾クラス、射撃間隔、基礎ダメージ、初回射撃可否を初期化関数
UFireComponent::UFireComponent() : m_bullet(nullptr), m_fireRate(2.f), m_damage(5.f), m_canBeFired(true)
{
	//コンポーネントの Tick を無効化
	PrimaryComponentTick.bCanEverTick = false;
}

//コンポーネント開始時にActorComponentの標準初期化を実行する関数
void UFireComponent::BeginPlay()
{
	//親クラスのBeginPlay()を呼ぶ
	Super::BeginPlay();
}

//射撃間隔を確認し、前方トレースのダメージと視認用の敵弾生成を一回実行する関数
void UFireComponent::FireBullet()
{
	//射撃可能かどうかを確認する
	if (!m_canBeFired) { return; }

	//所有者の敵キャラクターを取得する
	AActor *pEnemy = GetOwner();
	//敵が有効な場合だけ射線上の対象へダメージを適用する
	if (!m_bullet || !pEnemy) { return; }

	//衝突または射線判定を開始する位置
	FVector start = pEnemy->GetActorLocation();
	FVector end = start + pEnemy->GetActorForwardVector() * 2000.f;

	//ラインの衝突判定を行い、ヒットしたアクターにダメージを適用する
	FHitResult hitResult;
	//射線上の命中対象を確定するために使用するparams
	FCollisionQueryParams params;
	params.AddIgnoredActor(pEnemy);

	//ラインの衝突判定を行い、ヒットしたアクターにダメージを適用する
	bool bHit = GetWorld()->LineTraceSingleByChannel(hitResult, start, end, ECC_Visibility, params);

	if (bHit && hitResult.GetActor())
	{
		UGameplayStatics::ApplyDamage(hitResult.GetActor(), m_damage, pEnemy->GetInstigatorController(), pEnemy, nullptr);
	}

	//敵の正面へ視認用の弾丸を生成し、後で一括破棄できる配列へ登録する。
	FVector spawnLocation = pEnemy->GetActorLocation() + pEnemy->GetActorForwardVector() * 100.f;
	FRotator spawnRotation = pEnemy->GetActorRotation();

	//弾丸生成時のパラメータを設定する
	FActorSpawnParameters spawnParams;
	spawnParams.Owner = pEnemy;
	spawnParams.Instigator = pEnemy->GetInstigator();

	//敵弾を生成し、生成に成功した場合は追跡配列に追加する
	AEnemyBullet *pSpawnedBullet = GetWorld()->SpawnActor<AEnemyBullet>(m_bullet, spawnLocation, spawnRotation, spawnParams);
	if (pSpawnedBullet) { m_activeBullet.Add(pSpawnedBullet); }

	//射撃間隔を設定し、次弾の発射を許可するタイマーを開始する
	GetWorld()->GetTimerManager().SetTimer(m_resetFire, this, &UFireComponent::ResetFire, m_fireRate, false);

	//射撃可能フラグをfalseに設定し、次の射撃まで待機する
	m_canBeFired = false;
}

//射撃間隔の終了時に次弾の発射を許可する関数
void UFireComponent::ResetFire()
{
	m_canBeFired = true;
}

//このコンポーネントが生成した有効な敵弾を破棄し、追跡配列を空にする関数
void UFireComponent::ClearBullet()
{
	//射線上の命中対象を確定するため、有効状態を順番に更新する
	for (AEnemyBullet *enemyBullet : m_activeBullet)
	{
		if (IsValid(enemyBullet)) { enemyBullet->Destroy(); }
	}
	m_activeBullet.Empty();
}
