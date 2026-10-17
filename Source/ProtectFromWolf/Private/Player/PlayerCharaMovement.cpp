

#include "Player/PlayerChara.h"
#include "Audio/FootstepAttenuation.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Combat/CombatComponent.h"
#include "Combat/MeleeAttack.h"
#include "Weapons/Gun.h"
#include "Components/WidgetComponent.h"
#include "UI/PlayerUI.h"
#include "Enemy/EnemyChara.h"
#include "Combat/AmmoReload.h"
#include "Player/PlayerAds.h"
#include "Player/PlayerAnimationComponent.h"
#include "Player/WerewolfFormComponent.h"
#include "Player/PlayerCinematicComponent.h"
#include "Kismet/GameplayStatics.h"

//カメラ To Human ソケットの装着位置と使用状態を切り替える関数
void APlayerChara::AttachCameraToHumanSocket()
{
	//カメラが無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!m_springArm || !m_camera || !GetMesh()) { return; }

	if (!GetMesh()->DoesSocketExist(m_cameraSocketName))
	{
		m_camera->AttachToComponent(RootComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		return;
	}

	m_camera->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, m_cameraSocketName);
	m_camera->SetUsingAbsoluteLocation(true);
	m_camera->SetUsingAbsoluteRotation(true);
	m_camera->SetUsingAbsoluteScale(true);

	const FQuat neutralSocketWorldRotation = GetMesh()->GetComponentQuat() * m_defaultHeadSocketRotation.Quaternion();
	const FVector neutralSocketWorldLocation = GetMesh()->GetComponentTransform().TransformPosition(m_defaultHeadSocketLocation);
	const FVector cameraBaseOffsetWorld = neutralSocketWorldRotation.RotateVector(m_humanCameraBaseLocation);
	const FVector viewForward = FRotationMatrix(FRotator(0.f, m_cameraLookRotation.Yaw, 0.f)).GetUnitAxis(EAxis::X);
	const FVector cameraOffsetWorld = cameraBaseOffsetWorld + (viewForward * m_cameraForwardOffset) + (FVector::UpVector * m_cameraHeightOffset);
	m_camera->SetWorldLocation(neutralSocketWorldLocation + cameraOffsetWorld);
	m_camera->SetWorldRotation(m_cameraLookRotation);
	m_smoothedHeadRotationDelta = FQuat::Identity;

	m_springArm->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, m_cameraSocketName);
	m_springArm->SetRelativeLocation(FVector::ZeroVector);
	m_springArm->SocketOffset = FVector::ZeroVector;
	m_springArm->TargetArmLength = 0.f;
	m_springArm->bDoCollisionTest = false;
}

//HeadVisibleを指定内容へ更新する関数
void APlayerChara::SetHeadVisible(bool _visible)
{
	USkeletalMeshComponent *mesh = GetMesh();
	//Meshが無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!mesh) { return; }

	if (mesh->GetBoneIndex(m_headBoneName) == INDEX_NONE)
	{
		return;
	}

	if (_visible) { mesh->UnHideBoneByName(m_headBoneName); }
	else { mesh->HideBoneByName(m_headBoneName, PBO_None); }
}

//狼男形態のカメラをTPS用ルートへ取り付ける関数
void APlayerChara::AttachCameraToWerewolfRoot()
{
	//カメラが無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!m_springArm || !m_camera || !RootComponent) { return; }

	const FVector cameraPivotOffset = m_werewolfFormComponent ? m_werewolfFormComponent->GetCameraPivotOffset() : FVector(0.f, 0.f, 160.f);
	const FVector cameraSocketOffset = m_werewolfFormComponent ? m_werewolfFormComponent->GetCameraSocketOffset() : FVector(0.f, 60.f, 20.f);
	const float cameraArmLength = m_werewolfFormComponent ? m_werewolfFormComponent->GetCameraArmLength() : m_maxArmLength;
	const FVector2D pitchLimits = m_werewolfFormComponent ? m_werewolfFormComponent->GetCameraPitchLimits() : m_cameraPitchLimit;
	m_cameraLookRotation.Pitch = FMath::Clamp(FMath::UnwindDegrees(m_cameraLookRotation.Pitch), pitchLimits.X, pitchLimits.Y);
	m_cameraLookRotation.Roll = 0.f;

	m_springArm->AttachToComponent(RootComponent, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	m_springArm->SetUsingAbsoluteRotation(true);
	m_springArm->SetRelativeLocation(cameraPivotOffset);
	m_springArm->SetWorldRotation(m_cameraLookRotation);
	m_springArm->TargetArmLength = cameraArmLength;
	m_springArm->SocketOffset = cameraSocketOffset;
	m_springArm->bDoCollisionTest = true;

	m_camera->SetUsingAbsoluteLocation(false);
	m_camera->SetUsingAbsoluteRotation(false);
	m_camera->SetUsingAbsoluteScale(false);
	m_camera->AttachToComponent(m_springArm, FAttachmentTransformRules::SnapToTargetNotIncludingScale, USpringArmComponent::SocketName);
	m_camera->SetRelativeLocation(FVector::ZeroVector);
	m_camera->SetRelativeRotation(FRotator::ZeroRotator);
}

//カメラを更新する関数
void APlayerChara::UpdateCamera(float _deltaTime)
{
	//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
	if (m_playerCinematicComponent && m_playerCinematicComponent->IsPlaying()) { return; }
	//時間が無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (_deltaTime <= UE_SMALL_NUMBER || !m_springArm) { return; }

	//対象へ安全にアクセスする参照
	const float rotateCorrection = 60.f * _deltaTime;

	//カメラを正面へ向けるための回転
	constexpr float cameraRotationSpeed = 1.f;

	m_cameraLookRotation.Yaw += m_cameraRotateInput.X * rotateCorrection * cameraRotationSpeed;
	const FVector2D activePitchLimits = m_enhancedState && m_werewolfFormComponent ? m_werewolfFormComponent->GetCameraPitchLimits()
																				  : FVector2D(m_humanLookDownLimit, m_cameraPitchLimit.Y);
	m_cameraLookRotation.Pitch = FMath::Clamp(m_cameraLookRotation.Pitch + (m_cameraRotateInput.Y * rotateCorrection * cameraRotationSpeed),
											  activePitchLimits.X, activePitchLimits.Y);

	m_cameraLookRotation.Normalize();
	m_cameraLookRotation.Roll = 0.f;
	m_springArm->SetWorldRotation(m_cameraLookRotation);
	if (!m_enhancedState && m_camera)
	{
		const bool isAttachedToHumanCameraSocket =
			m_camera->GetAttachParent() == GetMesh() && m_camera->GetAttachSocketName() == m_cameraSocketName;
		if (!isAttachedToHumanCameraSocket) { AttachCameraToHumanSocket(); }

		if (GetMesh() && GetMesh()->DoesSocketExist(m_cameraSocketName))
		{
			const FQuat neutralSocketWorldRotation = GetMesh()->GetComponentQuat() * m_defaultHeadSocketRotation.Quaternion();
			const FVector neutralSocketWorldLocation = GetMesh()->GetComponentTransform().TransformPosition(m_defaultHeadSocketLocation);
			const FVector cameraBaseOffsetWorld = neutralSocketWorldRotation.RotateVector(m_humanCameraBaseLocation);
			const FVector viewForward = FRotationMatrix(FRotator(0.f, m_cameraLookRotation.Yaw, 0.f)).GetUnitAxis(EAxis::X);
			const FVector cameraOffsetWorld =
				cameraBaseOffsetWorld + (viewForward * m_cameraForwardOffset) + (FVector::UpVector * m_cameraHeightOffset);
			m_camera->SetWorldLocation(neutralSocketWorldLocation + cameraOffsetWorld);
		}

		//リロードや移動アニメーションで視点が傾かないようカメラ回転を入力だけから決定する処理
		//最終カメラ回転
		FRotator finalCameraRotation = m_cameraLookRotation;
		finalCameraRotation.Roll = 0.f;
		finalCameraRotation.Pitch = FMath::Clamp(FMath::UnwindDegrees(finalCameraRotation.Pitch), m_humanLookDownLimit, m_cameraPitchLimit.Y);
		m_camera->SetWorldRotation(finalCameraRotation);
		m_smoothedHeadRotationDelta = FQuat::Identity;
	}

	const float targetArmLength = m_enhancedState && m_werewolfFormComponent ? m_werewolfFormComponent->GetCameraArmLength() : m_defaultCameraArmLength;
	m_springArm->TargetArmLength = FMath::FInterpTo(m_springArm->TargetArmLength, targetArmLength, _deltaTime, m_cameraTransitionSpeed);

	const FVector targetSocketOffset =
		m_enhancedState && m_werewolfFormComponent ? m_werewolfFormComponent->GetCameraSocketOffset() : m_defaultCameraSocketOffset;
	m_springArm->SocketOffset = FMath::VInterpTo(m_springArm->SocketOffset, targetSocketOffset, _deltaTime, m_cameraTransitionSpeed);

	const FVector targetRelativeLocation = m_enhancedState && m_werewolfFormComponent ? m_werewolfFormComponent->GetCameraPivotOffset() : m_humanCameraOffset;
	m_springArm->SetRelativeLocation(
		FMath::VInterpTo(m_springArm->GetRelativeLocation(), targetRelativeLocation, _deltaTime, m_cameraTransitionSpeed));
}

//Moveを最新の入力と状態へ同期する関数
void APlayerChara::UpdateMove(float _deltaTime)
{

	if (m_isDead) { return; }
	if (m_enhancedState && m_attacking)
	{

		if (UCharacterMovementComponent *movement = GetCharacterMovement()) { movement->StopMovementImmediately(); }
		return;
	}
	//狼男の体、攻撃方向、歩行方向の計算を同じ正面へ揃える
	if (m_enhancedState && m_springArm)
	{
		const FRotator facing(0.f, m_springArm->GetComponentRotation().Yaw, 0.f);
		SetActorRotation(facing);
		if (AController *controller = GetController()) { controller->SetControlRotation(facing); }
	}
	if (!m_charaMoveInput.IsZero())
	{

		if (m_canControl == false) { return; }
		FVector newLocation = GetActorLocation();

		{

			//見下ろしても歩く速さが変わらないよう水平な移動基準を使う
			const FRotator heading(0.f, m_springArm->GetComponentRotation().Yaw, 0.f);
			const FVector forwardVec = FRotationMatrix(heading).GetUnitAxis(EAxis::X);
			AddMovementInput(forwardVec, m_charaMoveInput.Y * m_moveSpeed);
			const FVector rightVec = FRotationMatrix(heading).GetUnitAxis(EAxis::Y);
			AddMovementInput(rightVec, m_charaMoveInput.X * m_moveSpeed);
		}
	}

	{

		USkeletalMeshComponent *pMeshComp = GetMesh();
		if (pMeshComp == NULL) { return; }
		float newYaw = m_springArm->GetComponentRotation().Yaw - 90;

		//親の回転が二重に加算されないよう、体の向きをワールド座標で指定する
		pMeshComp->SetWorldRotation(FRotator(pMeshComp->GetComponentRotation().Pitch, newYaw, pMeshComp->GetComponentRotation().Roll));
	}
}

//ダメージを最新の入力と状態へ同期する関数
void APlayerChara::UpdateDamage(float _deltaTime)
{
	//ダメージが無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!m_damaged) { return; }
	UPlayerUI *healthBar = Cast<UPlayerUI>(m_playerUiWidgetComp->GetUserWidgetObject());

	m_damageTimeCnt += _deltaTime;

	healthBar->UpdateHealthUI();

	if (m_damageTimeCnt >= m_damageCooltime)
	{

		m_damageTimeCnt = 0.f;
		m_damaged = false;
	}

	//対象のHP状態に合わせて生存または死亡処理へ分岐する
	if (m_health <= 0) { m_isDead = true; }
}

//狼男状態を更新する関数
void APlayerChara::UpdateEnhanced(float _deltaTime)
{
	if (m_isDead) { return; }
	if (m_transformationInProgress) { return; }
	//状態が無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!m_enhancedState) { return; }

	m_enhancedTime = FMath::Max(0.f, m_enhancedTime - _deltaTime);
	const float remainingRatio = m_transformationDuration > UE_SMALL_NUMBER ? m_enhancedTime / m_transformationDuration : 0.f;
	m_enhanceGauge = FMath::RoundToInt(m_maxEnhanceGauge * remainingRatio);

	//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
	if (m_playerUi) { m_playerUi->UpdateEnhanceGauge(); }

	if (m_enhancedTime <= 0.f) { EndWerewolfTransformation(); }
}

//移動速度と接地状態に合わせて足音を再生する関数
void APlayerChara::UpdateFootstepAudio(float _deltaTime)
{
	if (m_isDead || !GetCharacterMovement() || GetCharacterMovement()->IsFalling())
	{
		m_footstepAudioTimer = 0.f;
		return;
	}

	const float speed = GetVelocity().Size2D();
	if (speed < 35.f)
	{
		m_footstepAudioTimer = 0.f;
		return;
	}

	m_footstepAudioTimer -= _deltaTime;
	if (m_footstepAudioTimer > 0.f) { return; }
	USoundBase *stepSound = m_useAlternateFootstep ? m_playerFootstepB.Get() : m_playerFootstepA.Get();
	if (stepSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, stepSound, GetActorLocation(), 0.32f,
			m_enhancedState ? FMath::FRandRange(0.78f, 0.86f) : FMath::FRandRange(0.96f, 1.04f), 0.f, GetMutableDefault<UFootstepAttenuation>());
	}
	AEnemyChara::BroadcastPlayerNoise(this, this, GetActorLocation(), m_enhancedState ? 2300.f : (speed > 700.f ? 1900.f : 1300.f));
	m_useAlternateFootstep = !m_useAlternateFootstep;
	m_footstepAudioTimer = speed > 700.f ? 0.31f : 0.43f;
}

//狼男変身を開始する関数
void APlayerChara::StartWerewolfTransformation()
{
	if (m_enhancedState || m_isDead || m_transformationInProgress) { return; }
	if (m_enhanceGauge < m_maxEnhanceGauge) { return; }
	if (!m_werewolfFormComponent)
	{
		return;
	}

	//弾薬の弾薬状態に合わせて射撃または補充処理へ分岐する
	if (m_ammoReloadComponent && m_ammoReloadComponent->m_isReloading) { m_ammoReloadComponent->CancelReload(); }
	if (m_adsComponent) { m_adsComponent->StopADS(); }
	//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
	if (m_playerAnimationComponent) { m_playerAnimationComponent->CancelActionAnimation(); }
	m_transformationInProgress = true;
	//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
	if (m_playerCinematicComponent && m_playerCinematicComponent->StartTransformation()) { return; }

	//演出カメラを生成できない場合でも変身処理を継続できるようにする処理
	if (!CommitWerewolfTransformationForm())
	{
		m_transformationInProgress = false;
		return;
	}
	m_transformationInProgress = false;
	m_canControl = true;
}

//狼男変身形態を確定する関数
bool APlayerChara::CommitWerewolfTransformationForm()
{
	//狼男が無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (m_enhancedState || m_isDead || !m_werewolfFormComponent) { return false; }
	if (!m_werewolfFormComponent->EnterForm(this))
	{
		return false;
	}
	//狼男へ変身した時だけ最大100の範囲でHPを25回復する処理
	constexpr int32 playerHealthCap = 100;
	m_maxHealth = playerHealthCap;
	m_health = FMath::Clamp(m_health, 0, m_maxHealth);
	//対象のHP状態に合わせて生存または死亡処理へ分岐する
	if (m_health < m_maxHealth)
	{
		//担当機能の状態を更新するために使用するHP
		const int32 previousHealth = m_health;
		const int32 recoveryAmount = FMath::RoundToInt(static_cast<float>(m_maxHealth) * m_werewolfFormComponent->GetHealthRecoveryPercent());
		m_health = FMath::Clamp(previousHealth + recoveryAmount, 0, m_maxHealth);
		//プレイヤーのHP状態に合わせて生存または死亡処理へ分岐する
		if (m_playerUi) m_playerUi->UpdateHealthUI();
	}
	SetHeadVisible(true);
	AttachCameraToWerewolfRoot();

	m_enhancedState = true;
	m_coolDown = false;
	m_impulse = false;
	m_enhancedTime = m_transformationDuration;
	m_enhancedHoldTime = 0.f;
	m_enhancedAxisValue = 0.f;
	m_attacking = false;
	m_bfastInput = false;
	m_currentComboIndex = 0;

	if (m_gun)
	{
		m_gun->SetActorHiddenInGame(true);
		m_gun->SetActorEnableCollision(false);
	}

	m_moveSpeed = 1.f;

	AddRadialForces();

	//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
	if (m_playerUi) { m_playerUi->UpdateEnhanceGauge(); }
	return true;
}

//狼男変身を終了する関数
void APlayerChara::EndWerewolfTransformation()
{
	//Transformが無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!m_enhancedState || m_transformationInProgress) { return; }

	if (m_combatComponent && m_combatComponent->m_meleeAttack) { m_combatComponent->m_meleeAttack->ResetCombo(); }

	m_enhancedState = false;
	m_coolDown = false;
	m_impulse = false;
	m_enhancedTime = m_transformationDuration;
	m_enhanceGauge = 0;
	m_enhancedHoldTime = 0.f;
	m_enhancedAxisValue = 0.f;
	m_attacking = false;
	m_bfastInput = false;
	m_currentComboIndex = 0;

	if (m_werewolfFormComponent)
	{
		m_werewolfFormComponent->ExitForm(this, m_defaultMesh, m_defaultAnimationClass, m_normalWalkSpeed, m_defaultJumpVelocity);
	}
	AttachCameraToHumanSocket();
	SetHeadVisible(false);
	m_springArm->bDoCollisionTest = false;

	if (m_gun)
	{
		m_gun->SetActorHiddenInGame(false);
		m_gun->SetActorEnableCollision(true);
	}

	m_moveSpeed = m_normalMoveInputScale;

	//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
	if (m_playerUi)
	{
		m_playerUi->UpdateEnhanceGauge();
		m_playerUi->UpdateAmmoUI();
	}
}

//変身ゲージを追加する関数
void APlayerChara::AddEnhanceGauge(int32 _amount)
{
	if (_amount <= 0 || m_enhancedState || m_isDead) { return; }

	m_enhanceGauge = FMath::Clamp(m_enhanceGauge + _amount, 0, m_maxEnhanceGauge);
	//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
	if (m_playerUi) { m_playerUi->UpdateEnhanceGauge(); }
}

//EnhanceGaugeRatioの現在値を参照側へ渡す関数
float APlayerChara::GetEnhanceGaugeRatio() const
{
	if (m_maxEnhanceGauge <= 0) { return 0.f; }
	return FMath::Clamp(static_cast<float>(m_enhanceGauge) / static_cast<float>(m_maxEnhanceGauge), 0.f, 1.f);
}

//放射状の衝撃を追加する関数
void APlayerChara::AddRadialForces()
{
	if (!GetWorld()) { return; }
	m_impulse = true;
	//変身の周囲にいる敵を壁や他の敵に遮られず候補として集める
	TArray<FHitResult> hits;
	const FVector location = GetActorLocation();
	FCollisionQueryParams query;
	query.AddIgnoredActor(this);
	GetWorld()->SweepMultiByObjectType(hits, location, location, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(FMath::Max(1.f, m_radius)), query);
	//メッシュとカプセルの両方へ接触しても同じ敵を繰り返し押し出さない
	TSet<AEnemyChara *> affected;
	for (const FHitResult &hit : hits)
	{
		AEnemyChara *enemy = Cast<AEnemyChara>(hit.GetActor());
		if (!IsValid(enemy) || enemy->GetHealthRatio() <= 0.f || affected.Contains(enemy)) { continue; }
		affected.Add(enemy);
		//変身の衝撃は壁を通過させない
		FCollisionQueryParams sightQuery;
		sightQuery.AddIgnoredActor(this);
		sightQuery.AddIgnoredActor(enemy);
		FHitResult wall;
		if (GetWorld()->LineTraceSingleByChannel(wall, location, enemy->GetActorLocation(), ECC_Visibility, sightQuery)) { continue; }
		FVector direction = (enemy->GetActorLocation() - location).GetSafeNormal2D();
		if (direction.IsNearlyZero()) { direction = GetActorForwardVector().GetSafeNormal2D(); }
		//変身は足元の空間を確保する弱い押し戻しに留め、最後のキックより短くする
		const float scale = enemy->m_enemyRank == EEnemyRank::MiddleBoss ? 0.45f : enemy->m_enemyRank == EEnemyRank::LastBoss ? 0.22f : 1.f;
		enemy->ApplyKnockback(direction * (300.f * scale) + FVector::UpVector * 120.f);
	}
}

//上下入力を視点のピッチ回転へ反映する関数
void APlayerChara::Cam_RotatePitch(float _axisValue)
{
	//対象が無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!m_canControl) { return; }
	m_cameraRotateInput.Y = _axisValue;
}

//左右入力を視点のヨー回転へ反映する関数
void APlayerChara::Cam_RotateYaw(float _axisValue)
{
	//対象が無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!m_canControl) { return; }
	m_cameraRotateInput.X = _axisValue;
}

//前後入力をカメラ基準の移動方向へ反映する関数
void APlayerChara::Chara_MoveForward(float _axisValue)
{
	//対象が無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!m_canControl) { return; }

	m_charaMoveInput.Y = FMath::Clamp(_axisValue, -1.0f, 1.0f) * 0.5;
}

//左右入力をカメラ基準の移動方向へ反映する関数
void APlayerChara::Chara_MoveRight(float _axisValue)
{
	//対象が無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!m_canControl) { return; }

	m_charaMoveInput.X = FMath::Clamp(_axisValue, -1.0f, 1.0f) * 0.5;
}

//人間形態と狼男形態を切り替えて移動速度とCameraを更新する関数
void APlayerChara::Enhancedstate(float _axisValue)
{
	//対象が無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!m_canControl) { return; }
	if (m_enhancedState) { return; }
	if (m_enhanceGauge < m_maxEnhanceGauge) { return; }

	m_enhancedAxisValue = _axisValue;

	if (m_enhancedAxisValue >= m_pressThreshold)
	{
		m_enhancedHoldTime += GetWorld()->GetDeltaSeconds();

		if (m_enhancedHoldTime >= m_transformationHoldDuration) { StartWerewolfTransformation(); }
	}
	else
	{
		m_enhancedAxisValue = 0.f;
		m_enhancedHoldTime = 0.f;
	}
}

//ジャンプ入力を開始する関数
void APlayerChara::JumpStart()
{
	//対象が無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!m_canControl || m_isDead) { return; }
	//移動Componentが無効な場合は入力方向に合う移動とCamera回転を実行できないため終了する
	if (!GetCharacterMovement() || GetCharacterMovement()->IsFalling()) { return; }

	//弾薬の弾薬状態に合わせて射撃または補充処理へ分岐する
	if (m_ammoReloadComponent && m_ammoReloadComponent->m_isReloading) { m_ammoReloadComponent->CancelReload(); }
	if (m_playerAnimationComponent && !m_enhancedState) { m_playerAnimationComponent->OnJumpStarted(); }

	Jump();
}

//ジャンプ入力を終了する関数
void APlayerChara::JumpStop()
{
	StopJumping();
}
