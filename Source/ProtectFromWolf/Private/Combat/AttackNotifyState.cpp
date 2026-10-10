

#include "Combat/AttackNotifyState.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Player/PlayerChara.h"
#include "Combat/CombatComponent.h"
#include "Combat/MeleeAttack.h"
#include "Combat/MeleeAttackHitBox.h"
#include "Enemy/EnemyChara.h"

//アニメーション通知時呼ばれる関数
void UAttackNotifyState::NotifyBegin(USkeletalMeshComponent *_meshComp, UAnimSequenceBase *_animation, float _totalDuration,
									 const FAnimNotifyEventReference &_eventReference)
{
	//メッシュコンポーネントが存在しない場合は処理を終了する
	if (!_meshComp) { return; }

	//メッシュコンポーネントの所有者を取得する
	AActor *owner = _meshComp->GetOwner();
	if (AEnemyChara *enemy = Cast<AEnemyChara>(owner))
	{
		enemy->OpenMeleeHitWindow();
		return;
	}
	//所有者がプレイヤーキャラクターであるかを確認する
	auto player = Cast<APlayerChara>(owner);

	//プレイヤーキャラクターが存在しない場合、またはコンバットコンポーネントや近接攻撃が存在しない場合は処理を終了する
	if (!player || !player->m_combatComponent || !player->m_combatComponent->m_meleeAttack) { return; }

	player->m_combatComponent->m_meleeAttack->OpenHitWindow();
}

//アニメーション終了時呼ばれる関数
void UAttackNotifyState::NotifyEnd(USkeletalMeshComponent *_meshComp, UAnimSequenceBase *_animation, const FAnimNotifyEventReference &_eventReference)
{
	//メッシュコンポーネントが存在しない場合は処理を終了する
	if (!_meshComp) { return; }

	//メッシュコンポーネントの所有者を取得する
	AActor *owner = _meshComp->GetOwner();
	if (AEnemyChara *enemy = Cast<AEnemyChara>(owner))
	{
		enemy->CloseMeleeHitWindow();
		return;
	}

	//所有者がプレイヤーキャラクターであるかを確認する
	auto player = Cast<APlayerChara>(owner);

	//プレイヤーキャラクターが存在しない場合、またはコンバットコンポーネントや近接攻撃が存在しない場合は処理を終了する
	if (!player || !player->m_combatComponent || !player->m_combatComponent->m_meleeAttack) { return; }

	//プレイヤーキャラクターのコンバットコンポーネントの近接攻撃のヒットウィンドウを閉じる
	player->m_combatComponent->m_meleeAttack->CloseHitWindow();
}
