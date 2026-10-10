

#include "Combat/CombAnimNotifyState.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Player/PlayerChara.h"
#include "Combat/CombatComponent.h"
#include "Combat/MeleeAttack.h"

//アニメーション通知時呼ばれる関数
void UCombAnimNotifyState::NotifyBegin(USkeletalMeshComponent *_meshComp, UAnimSequenceBase *_animation, float _totalDuration,
									   const FAnimNotifyEventReference &_eventReference)
{
	//メッシュコンポーネントが存在しない場合は処理を終了する
	if (!_meshComp) { return; }

	//メッシュコンポーネントの所有者を取得する
	AActor *owner = _meshComp->GetOwner();
	//狼男Comboの入力受付を更新するために使用するプレイヤー
	auto player = Cast<APlayerChara>(owner);

	//プレイヤーキャラクターが存在しない場合、またはコンバットコンポーネントや近接攻撃が存在しない場合は処理を終了する
	if (!player || !player->m_combatComponent || !player->m_combatComponent->m_meleeAttack) { return; }

	player->m_combatComponent->m_meleeAttack->OpenComboWindow();
}

//アニメーション終了時呼ばれる関数
void UCombAnimNotifyState::NotifyEnd(USkeletalMeshComponent *_meshComp, UAnimSequenceBase *_animation,
									 const FAnimNotifyEventReference &_eventReference)
{
	//メッシュコンポーネントが存在しない場合は処理を終了する
	if (!_meshComp) { return; }

	//メッシュコンポーネントの所有者を取得する
	AActor *owner = _meshComp->GetOwner();
	//狼男Comboの入力受付を更新するために使用するプレイヤー
	auto player = Cast<APlayerChara>(owner);

	//プレイヤーキャラクターが存在しない場合、またはコンバットコンポーネントや近接攻撃が存在しない場合は処理を終了する
	if (!player || !player->m_combatComponent || !player->m_combatComponent->m_meleeAttack) { return; }

	//コンボ受付時間を閉じる
	player->m_combatComponent->m_meleeAttack->CloseComboWindow();
}
