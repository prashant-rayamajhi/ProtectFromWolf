#include "Combat/MeleeComponent.h"
#include "Enemy/EnemyChara.h"
#include "GameFramework/DamageType.h"
#include "Player/PlayerChara.h"
#include "Kismet/GameplayStatics.h"

//MeleeComponentが使用するComponentと初期値を構築する関数
UMeleeComponent::UMeleeComponent() : m_attackRange(120.f), m_meleeInterval(2.0f), m_meleeDamage(5.f), m_canBeMelee(true)
{
}

//コンポーネント開始時にActorComponentの標準初期化を実行する関数
void UMeleeComponent::BeginPlay()
{
	//親クラスのBeginPlay()を呼ぶ
	Super::BeginPlay();
}

//所有者前方の近接範囲を検査し、最初に命中した有効な敵へダメージを与える関数
void UMeleeComponent::MeleeAttack()
{
	//近接攻撃が可能かどうかを確認する
	if (!m_canBeMelee) { return; }

	//所有者の敵キャラクターを取得する
	AEnemyChara *pEnemy = Cast<AEnemyChara>(GetOwner());
	//敵が有効な場合だけ担当機能を実行する
	if (!pEnemy) { return; }

	//ダメージ量が0以下の場合はデフォルト値を使用する
	float actualDamage = m_meleeDamage > 0.f ? m_meleeDamage : 10.f;

	//プレイヤーキャラクターを取得する
	AActor *target = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	//攻撃対象が有効な場合だけ担当機能を実行する
	if (!target) { return; }

	//距離へのターゲット
	float distToTarget = FVector::Dist(pEnemy->GetActorLocation(), target->GetActorLocation());

	//キャラクター同士のカプセルの厚みを考慮し範囲に百の猶予を持たせる
	float hitRange = m_attackRange + 100.f;
	if (pEnemy->m_enemyRank == EEnemyRank::LastBoss) { hitRange = m_attackRange; }

	//ターゲットが命中範囲内にいるかどうかを確認する
	if (distToTarget <= hitRange)
	{
		//ターゲットが所有者の前方にいるかどうかを確認する
		FVector dirToTarget = (target->GetActorLocation() - pEnemy->GetActorLocation()).GetSafeNormal2D();
		FVector forwardDir = pEnemy->GetActorForwardVector().GetSafeNormal2D();

		//ターゲットが所有者の前方にいるかどうかを確認するために、前方ベクトルとターゲットへの方向ベクトルの内積を計算する
		float dot = FVector::DotProduct(forwardDir, dirToTarget);

		//ターゲットが所有者の前方にいる場合、ダメージを適用する
		if (dot >= 0.0f && UGameplayStatics::ApplyDamage(target, actualDamage, pEnemy->GetController(), pEnemy, UDamageType::StaticClass()) > 0.f)
		{
			pEnemy->ConfirmMeleeHit();
		}
	}
}

//ダメージを設定する処理を行う関数
void UMeleeComponent::SetDamage(float _damage)
{
	m_meleeDamage = _damage;
}

//攻撃範囲を設定する処理を行う関数
void UMeleeComponent::SetAttackRange(float _range)
{
	m_attackRange = _range;
}
