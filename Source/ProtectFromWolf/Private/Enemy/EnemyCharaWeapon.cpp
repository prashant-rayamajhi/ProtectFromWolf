#include "Enemy/EnemyChara.h"
#include "Combat/MeleeComponent.h"
#include "Animation/AnimInstance.h"
#include "Player/PlayerChara.h"
#include "AI/Controllers/EnemyAIController.h"
#include "Weapons/EnemyGun.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/WidgetComponent.h"
#include "UI/BossEnemyWidget.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/EnemyMeleeWeapon.h"
#include "Engine/DamageEvents.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

//未生成の銃も攻撃選択に含められるよう、生成クラスの設定を調べる関数
bool AEnemyChara::CanEquipGun() const
{
	return m_gunClass != nullptr;
}

//攻撃形式の変更に合わせて使用する武器を切り替える関数
void AEnemyChara::SwitchWeapon(EEnemyAttackStyle _newStyle)
{
	//指定Styleの武器が構え済みの場合は切り替えを繰り返さない
	if (m_currentStyle == _newStyle && m_weaponState == EWeaponState::Ready) { return; }
	//別の武器切り替えAnimation中は新しい要求を受け付けない
	if (m_isSwitchingWeapon) { return; }
	//銃を装備中の場合は武器切り替え前に射撃を停止する
	if (IsValid(m_currentGun)) { m_currentGun->StopFire(); }

	//攻撃中に切り替える場合は攻撃モンタージュと攻撃状態を解除する
	if (m_actionState == EActionState::Attacking && GetMesh() && GetMesh()->GetAnimInstance())
	{
		GetMesh()->GetAnimInstance()->StopAllMontages(0.15f);
		SetActionState(EActionState::Idle);
	}

	//レーザーStyleは手持ち武器を表示せず即座に準備完了へ移行する
	if (_newStyle == EEnemyAttackStyle::Laser)
	{
		m_currentStyle = _newStyle;
		SetWeaponVisibility(false);
		m_weaponState = EWeaponState::Ready;
		return;
	}

	m_isSwitchingWeapon = true;
	m_pendingWeaponStyle = _newStyle;

	//現在の武器が構え済みの場合は収納Animationから切り替えを開始する
	if (m_weaponState == EWeaponState::Ready) { HolsterWeapon(); }
	else { OnWeaponSwitchComplete(); }
}

//武器を生成する関数
void AEnemyChara::CreateWeapon(EEnemyAttackStyle _weaponType)
{
	//銃Styleでは未生成の場合だけ銃Actorを作成する
	if (_weaponType == EEnemyAttackStyle::Gun)
	{
		//既に銃を所有している場合は重複生成しない
		if (m_currentGun) { return; }
		//銃Classが設定されている場合だけActorを生成する
		if (m_gunClass)
		{
			//生成した銃のOwnerとInstigatorを敵へ設定するParameter
			FActorSpawnParameters spawnParams;
			spawnParams.Owner = this;
			spawnParams.Instigator = GetInstigator();

			m_currentGun = GetWorld()->SpawnActor<AEnemyGun>(m_gunClass, FVector::ZeroVector, FRotator::ZeroRotator, spawnParams);
			//生成成功時に銃を手のSocketへ装着して初期表示を無効にする
			if (m_currentGun)
			{
				m_currentGun->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, TEXT("WeaponSocket"));
				m_currentGun->SetActorHiddenInGame(true);
				m_currentGun->SetActorEnableCollision(false);
			}
		}
	}
	//近接Styleでは未生成の場合だけ近接武器Actorを作成する
	else if (_weaponType == EEnemyAttackStyle::Melee)
	{
		//既に近接武器を所有している場合は重複生成しない
		if (m_meleeWeapon) { return; }
		//近接武器Classが設定されている場合だけActorを生成する
		if (m_meleeWeaponClass)
		{
			//生成した近接武器のOwnerとInstigatorを敵へ設定するParameter
			FActorSpawnParameters spawnParams;
			spawnParams.Owner = this;
			spawnParams.Instigator = GetInstigator();

			m_meleeWeapon = GetWorld()->SpawnActor<AActor>(m_meleeWeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, spawnParams);
			//生成成功時に近接武器を手のSocketへ装着して初期表示を無効にする
			if (m_meleeWeapon)
			{
				m_meleeWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, TEXT("MeleeWeaponSocket"));
				m_meleeWeapon->SetActorHiddenInGame(true);
				m_meleeWeapon->SetActorEnableCollision(false);
			}
		}
	}
}

//武器切り替え完了の通知を受け取る関数
void AEnemyChara::OnWeaponSwitchComplete()
{
	m_currentStyle = m_pendingWeaponStyle;
	CreateWeapon(m_currentStyle);
	DrawWeapon();
	m_isSwitchingWeapon = false;
}

//指定した攻撃形式の武器を生成して手のSocketへ取り付ける関数
void AEnemyChara::EquipWeapon(EEnemyAttackStyle _newStyle)
{
	m_currentStyle = _newStyle;
	CreateWeapon(_newStyle);

	//近接Styleでは銃を隠して近接武器を表示する
	if (_newStyle == EEnemyAttackStyle::Melee)
	{
		//銃が有効な場合は射撃を停止して非表示にする
		if (IsValid(m_currentGun))
		{
			m_currentGun->StopFire();
			m_currentGun->SetActorHiddenInGame(true);
		}
		//近接武器が有効な場合は手元へ表示する
		if (IsValid(m_meleeWeapon)) m_meleeWeapon->SetActorHiddenInGame(false);
	}
	//銃Styleでは近接武器を隠して銃を表示する
	else if (_newStyle == EEnemyAttackStyle::Gun)
	{
		//近接武器が有効な場合は非表示にする
		if (IsValid(m_meleeWeapon)) m_meleeWeapon->SetActorHiddenInGame(true);
		//銃が有効な場合は手元へ表示する
		if (IsValid(m_currentGun)) m_currentGun->SetActorHiddenInGame(false);
	}

	m_weaponState = EWeaponState::Ready;

	//ボスRankは収納と構えAnimationを経由して武器を切り替える
	if (m_enemyRank == EEnemyRank::MiddleBoss || m_enemyRank == EEnemyRank::LastBoss) { SwitchWeapon(_newStyle); }
	else { m_currentStyle = _newStyle; }
}

//銃を待機位置から照準位置へ移して構え始める関数
void AEnemyChara::DrawWeapon()
{
	//収納済みでない武器へ構えAnimationを重ねて再生しない
	if (m_weaponState != EWeaponState::Holstered) { return; }
	m_weaponState = EWeaponState::Drawing;
	SetActionState(EActionState::DrawingWeapon);
	//武器操作音が設定されている場合は構え開始地点で再生する
	if (m_weaponHandlingSound) { UGameplayStatics::PlaySoundAtLocation(this, m_weaponHandlingSound, GetActorLocation(), 0.46f, 1.15f); }

	//通知のないモンタージュも再生終了で準備完了にし、再生できない場合はその場で復帰する
	UAnimMontage *montage = m_montageMap.FindRef(TEXT("DrawWeapon"));
	UAnimInstance *animation = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (montage && animation && animation->Montage_Play(montage) > 0.f)
	{
		FOnMontageEnded ended;
		ended.BindWeakLambda(this, [this](UAnimMontage *_finished, bool _interrupted)
		{
			if (!_interrupted && GetHealthRatio() > 0.f && m_weaponState == EWeaponState::Drawing) { OnWeaponDrawComplete(); }
		});
		animation->Montage_SetEndDelegate(ended, montage);
	}
	else { OnWeaponDrawComplete(); }
}

//銃を照準位置から待機位置へ戻して収納する関数
void AEnemyChara::HolsterWeapon()
{
	//構え済みでない武器へ収納Animationを重ねて再生しない
	if (m_weaponState != EWeaponState::Ready) { return; }
	m_weaponState = EWeaponState::Holstering;
	SetActionState(EActionState::HolsteringWeapon);
	//武器操作音が設定されている場合は収納開始地点で再生する
	if (m_weaponHandlingSound) { UGameplayStatics::PlaySoundAtLocation(this, m_weaponHandlingSound, GetActorLocation(), 0.4f, 0.82f); }

	//収納モンタージュが登録されている場合はAnimation完了通知を待つ
	if (m_montageMap.Contains(TEXT("HolsterWeapon")))
	{
		//収納モンタージュを再生するAnimation Instance
		UAnimInstance *animInst = GetMesh()->GetAnimInstance();
		//Animation Instanceが有効な場合だけ収納モンタージュを再生する
		if (animInst) { animInst->Montage_Play(m_montageMap[TEXT("HolsterWeapon")]); }
	}
	else { OnWeaponHolsterComplete(); }
}

//銃をプレイヤーへ向けて射撃可能な照準状態へ移行する関数
void AEnemyChara::AimWeapon()
{
	//武器を構え終わるまでは照準状態へ移行しない
	if (m_weaponState != EWeaponState::Ready) { return; }
	SetActionState(EActionState::AimingWeapon);
}

//射撃中を停止する関数
void AEnemyChara::StopFiring()
{
	//銃が有効な場合はBurst Timerを含む射撃を停止する
	if (IsValid(m_currentGun)) { m_currentGun->StopFire(); }

	//攻撃状態の場合は射撃モンタージュも停止してIdleへ戻す
	if (m_actionState == EActionState::Attacking)
	{
		//射撃モンタージュを停止するAnimation Instance
		UAnimInstance *animInst = GetMesh()->GetAnimInstance();
		//Animationと射撃モンタージュが有効な場合だけ再生状態を調べる
		if (animInst && m_attackMontage)
		{
			//射撃モンタージュを再生中の場合はBlend時間を付けて停止する
			if (animInst->Montage_IsPlaying(m_attackMontage)) { animInst->Montage_Stop(0.2f, m_attackMontage); }
		}
		//跳躍や被弾の保護を通して状態を変更する
		SetActionState(EActionState::Idle);
	}
}

//射撃終了後に銃を下げて戦闘待機姿勢へ戻す関数
void AEnemyChara::LowerWeapon()
{
	//照準状態の場合だけ戦闘Idleへ戻す
	if (m_actionState == EActionState::AimingWeapon) { SetActionState(EActionState::Idle); }
}

//武器構え完了の通知を受け取る関数
void AEnemyChara::OnWeaponDrawComplete()
{
	m_weaponState = EWeaponState::Ready;
	//射撃セクションにある構え完了通知で進行中の射撃を解除しない
	if (m_actionState == EActionState::DrawingWeapon) { SetActionState(EActionState::Idle); }

	//敵本体が表示中の場合だけ装備中の武器を表示する
	if (!IsHidden()) { SetWeaponVisibility(true); }
}

//構えAnimation完了後に射撃可能状態へ移行する関数
void AEnemyChara::OnWeaponAimComplete()
{
	//構え上げ完了より前の発射通知は無視し、この通知から射撃を開始する
	BeginFireSequence();
}

//武器収納完了の通知を受け取る関数
void AEnemyChara::OnWeaponHolsterComplete()
{
	//古い攻撃モンタージュの収納通知で戦闘中の武器を隠さない
	if (m_weaponState != EWeaponState::Holstering && !m_isSwitchingWeapon) { return; }
	m_weaponState = EWeaponState::Holstered;
	SetActionState(EActionState::Idle);
	SetWeaponVisibility(false);

	//武器切り替え中の場合は次の武器生成と構えへ進む
	if (m_isSwitchingWeapon) { OnWeaponSwitchComplete(); }
}

//武器表示状態を設定する関数
void AEnemyChara::SetWeaponVisibility(bool _bVisible)
{
	//非表示要求では全武器の表示、Collision、攻撃判定を無効にする
	if (!_bVisible)
	{
		//銃が有効な場合は非表示にして射撃を停止する
		if (IsValid(m_currentGun))
		{
			m_currentGun->SetActorHiddenInGame(true);
			m_currentGun->SetActorEnableCollision(false);
			m_currentGun->StopFire();
		}
		//近接武器が有効な場合は非表示にして命中判定を停止する
		if (IsValid(m_meleeWeapon))
		{
			m_meleeWeapon->SetActorHiddenInGame(true);
			m_meleeWeapon->SetActorEnableCollision(false);
			//近接武器固有の命中判定を停止するためのCast結果
			AMeleeWeapon *meleeActor = Cast<AMeleeWeapon>(m_meleeWeapon);
			//近接武器としてCastできた場合は攻撃受付を解除する
			if (meleeActor) { meleeActor->DeactivateWeapon(); }
		}
	}
	else
	{
		//銃Styleでは銃だけを表示して発射物との自己衝突を無効にする
		if (m_currentStyle == EEnemyAttackStyle::Gun && IsValid(m_currentGun))
		{
			m_currentGun->SetActorHiddenInGame(false);
			//銃本体が発射直後の弾を遮らないようにCollisionを無効化する
			m_currentGun->SetActorEnableCollision(false);
			//近接武器が有効な場合は銃Style中に非表示にする
			if (IsValid(m_meleeWeapon))
			{
				m_meleeWeapon->SetActorHiddenInGame(true);
				m_meleeWeapon->SetActorEnableCollision(false);
			}
		}
		//近接Styleでは近接武器だけを表示して命中判定を利用可能にする
		else if (m_currentStyle == EEnemyAttackStyle::Melee && IsValid(m_meleeWeapon))
		{
			m_meleeWeapon->SetActorHiddenInGame(false);
			m_meleeWeapon->SetActorEnableCollision(true);
			//銃が有効な場合は近接Style中に非表示にする
			if (IsValid(m_currentGun))
			{
				m_currentGun->SetActorHiddenInGame(true);
				m_currentGun->SetActorEnableCollision(false);
			}
		}
	}
}
