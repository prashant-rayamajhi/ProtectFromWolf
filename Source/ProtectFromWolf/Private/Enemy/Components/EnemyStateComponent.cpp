#include "Enemy/Components/EnemyStateComponent.h"

//敵の行動状態と待機時間の初期値を設定する関数
UEnemyStateComponent::UEnemyStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	ResetState();
}

//状態を初期状態へ戻す関数
void UEnemyStateComponent::ResetState()
{
	//状態を初期状態へ戻す
	m_actionState = EActionState::Idle;
	m_weaponState = EWeaponState::Holstered;
	m_canSeeTarget = false;
	m_isReloading = false;
	m_isSwitchingWeapon = false;
}

//行動固定中か判定する関数
bool UEnemyStateComponent::IsActionLocked() const
{
	return IsAttacking() || IsReloading() || m_isSwitchingWeapon;
}
