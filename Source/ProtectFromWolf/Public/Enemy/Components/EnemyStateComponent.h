#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Enemy/EnemyChara.h"
#include "EnemyStateComponent.generated.h"

//敵の行動と武器と認識と遷移状態を管理するコンポーネント
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
//敵の行動、武器、視認、リロード、持替え状態を一元管理する部品
class PROTECTFROMWOLF_API UEnemyStateComponent : public UActorComponent
{
	GENERATED_BODY()

  public:
	//敵の行動状態と武器状態と待機時間を初期化する関数
	UEnemyStateComponent();

	//状態を初期状態へ戻す関数
	void ResetState();
	void SetActionState(EActionState _newState)
	{
		m_actionState = _newState;
	}
	void SetWeaponState(EWeaponState _newState)
	{
		m_weaponState = _newState;
	}
	void SetCanSeeTarget(bool _canSeeTarget)
	{
		m_canSeeTarget = _canSeeTarget;
	}
	void SetReloading(bool _isReloading)
	{
		m_isReloading = _isReloading;
	}
	void SetSwitchingWeapon(bool _isSwitching)
	{
		m_isSwitchingWeapon = _isSwitching;
	}

	EActionState GetActionState() const
	{
		return m_actionState;
	}
	EWeaponState GetWeaponState() const
	{
		return m_weaponState;
	}
	bool CanSeeTarget() const
	{
		return m_canSeeTarget;
	}
	bool IsReloading() const
	{
		return m_isReloading || m_actionState == EActionState::Reloading;
	}
	bool IsAttacking() const
	{
		return m_actionState == EActionState::Attacking;
	}
	bool IsSwitchingWeapon() const
	{
		return m_isSwitchingWeapon;
	}
	//行動固定中か判定する関数
	bool IsActionLocked() const;

  private:
	//待機と移動と攻撃を切り替える現在の行動状態
	EActionState m_actionState;
	//収納と構えと照準を切り替える現在の武器状態
	EWeaponState m_weaponState;
	//可能視認ターゲットかどうか
	bool m_canSeeTarget;
	//リロード中かどうか
	bool m_isReloading;
	//切り替え中武器かどうか
	bool m_isSwitchingWeapon;
};
