#include "Enemy/EnemyChara.h"
#include "Combat/MeleeComponent.h"
#include "Movement/CharacterPace.h"
#include "Enemy/EnemyTeleportPlacement.h"
#include "TimerManager.h"
#include "GameFramework/DamageType.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Player/PlayerChara.h"
#include "AI/Controllers/EnemyAIController.h"
#include "Weapons/EnemyGun.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/WidgetComponent.h"
#include "UI/BossEnemyWidget.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/EnemyMeleeWeapon.h"
#include "Engine/DamageEvents.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "EngineUtils.h"

//一時的なボスEffectを指定時間後に安全に停止するTimerを設定する関数
void AEnemyChara::ArmTransientEffectCleanup(UNiagaraComponent *_effect, float _maximumLifetime)
{
	//エフェクトが有効でない場合やワールドが存在しない場合は処理を中止する。
	if (!IsValid(_effect) || !GetWorld()) { return; }

	//Transient ボス Effectsを破棄または初期状態へ戻す。
	_effect->SetAutoDestroy(false);
	m_transientBossEffects.Add(_effect);
	m_transientBossEffectExpirationTimes.Add(GetWorld()->GetTimeSeconds() + FMath::Max(0.05f, _maximumLifetime));

	//破棄済みの敵を参照せず一時Effectを停止できるTimerを設定する
	if (!GetWorldTimerManager().IsTimerActive(m_transientBossEffectCleanupTimerHandle))
	{
		GetWorldTimerManager().SetTimer(m_transientBossEffectCleanupTimerHandle, this, &AEnemyChara::CleanupTransientBossEffects, 0.05f, true);
	}
}

//再生を終えたボス攻撃Effectを停止して破棄する関数
void AEnemyChara::CleanupTransientBossEffects()
{
	//現在のワールド時間を取得する
	const double now = GetWorld() ? GetWorld()->GetTimeSeconds() : TNumericLimits<double>::Max();

	//ボスエフェクトのリストを逆順にループして、期限切れまたは無効なエフェクトを破棄する
	for (int32 index = m_transientBossEffects.Num() - 1; index >= 0; --index)
	{
		//エフェクトの有効性と期限切れを確認する
		UNiagaraComponent *effect = m_transientBossEffects[index];
		const bool bExpired = !m_transientBossEffectExpirationTimes.IsValidIndex(index) || now >= m_transientBossEffectExpirationTimes[index];

		//エフェクトが無効または期限切れの場合、破棄または初期状態へ戻す
		if (!IsValid(effect) || bExpired)
		{
			//エフェクトが有効な場合、破棄または初期状態へ戻す
			if (IsValid(effect))
			{
				effect->SetAutoDestroy(false);
				effect->DeactivateImmediate();
				effect->DestroyComponent();
			}
			//リストからエフェクトと期限切れ時間を削除する
			m_transientBossEffects.RemoveAtSwap(index);
			//Effectと対応する有効期限が存在する場合は同じIndexから削除する
			if (m_transientBossEffectExpirationTimes.IsValidIndex(index)) { m_transientBossEffectExpirationTimes.RemoveAtSwap(index); }
		}
	}

	//エフェクトのリストが空の場合、期限切れ時間のリストをリセットし、タイマーをクリアする
	if (m_transientBossEffects.IsEmpty())
	{
		m_transientBossEffectExpirationTimes.Reset();
		GetWorldTimerManager().ClearTimer(m_transientBossEffectCleanupTimerHandle);
	}
}

//死亡または戦闘終了時に残っているボス攻撃Effectをすべて破棄する関数
void AEnemyChara::DestroyAllTransientBossEffects()
{
	//タイマーをクリアして、すべてのエフェクトを破棄または初期状態へ戻す
	GetWorldTimerManager().ClearTimer(m_transientBossEffectCleanupTimerHandle);
	//敵の破棄時に登録済みの一時Effectをすべて停止する
	for (UNiagaraComponent *effect : m_transientBossEffects)
	{
		//エフェクトが有効な場合、破棄または初期状態へ戻す
		if (IsValid(effect))
		{
			effect->SetAutoDestroy(false);
			effect->DeactivateImmediate();
			effect->DestroyComponent();
		}
	}
	//エフェクトのリストと期限切れ時間のリストをリセットする
	m_transientBossEffects.Reset();
	m_transientBossEffectExpirationTimes.Reset();
}

//攻撃対象が現在成立しているかを判定する関数
bool AEnemyChara::IsFacingTarget(const AActor *_enemy, const AActor *_target, float _facingAngleDegrees = 45.f)
{
	//エネミーとターゲットが有効でない場合は false を返す
	if (!_enemy || !_target) { return false; }

	//敵位置
	FVector enemyLocation = _enemy->GetActorLocation();
	FVector targetLocation = _target->GetActorLocation();

	//へのターゲット
	FVector toTarget = targetLocation - enemyLocation;
	toTarget.Z = 0.f;
	toTarget.Normalize();

	//前方
	FVector forward = _enemy->GetActorForwardVector();
	forward.Z = 0.f;
	forward.Normalize();

	//攻撃方向とターゲット方向の内積
	float dot = FVector::DotProduct(forward, toTarget);
	dot = FMath::Clamp(dot, -1.f, 1.f);
	float angleDegrees = FMath::Acos(dot) * 180.f / PI;

	//内積の角度が指定された facingAngleDegrees 以下であれば true を返す
	return angleDegrees <= _facingAngleDegrees;
}

//跳躍攻撃を実行する関数
void AEnemyChara::PerformLeapAttack()
{
	//アクション状態が Idle でない場合は処理を中止する
	if (m_currentStyle != EEnemyAttackStyle::Melee) { SwitchWeapon(EEnemyAttackStyle::Melee); }

	//通常歩行の減速分を除外し、跳躍攻撃の接近速度は従来どおりに保つ
	if (GetCharacterMovement()) { GetCharacterMovement()->MaxWalkSpeed = m_defaultMoveSpeed / (m_walkScale * CharacterPace::WalkScale) * 2.8f; }
}

//攻撃用のモンタージュがあるボスだけ着地衝撃を使用できるか調べる関数
bool AEnemyChara::CanGroundSmash() const
{
	//溜め姿勢を出せないボスに、見た目のない跳躍攻撃をさせない
	if (!GetMesh() || !GetMesh()->GetAnimInstance() || !IsValid(m_montageMap.FindRef(TEXT("GroundSmash")))) { return false; }
	//攻撃候補の採点段階から、実際に衝撃が届く相手だけを対象にする
	const AActor *target = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const AEnemyAIController *observer = Cast<AEnemyAIController>(GetController());
	return IsValid(target) && observer && observer->CanObserveTarget(target) &&
		FVector::Dist2D(GetActorLocation(), target->GetActorLocation()) <= m_groundSmashRadius;
}

//溜め姿勢の後に短く跳び、実際の着地点へ衝撃を発生させる関数
void AEnemyChara::PerformGroundSmash()
{
	if (m_actionState != EActionState::Idle || !GetWorld() || !CanGroundSmash() || IsKnockedBack()) { return; }
	//収納モーションが完了してから叩きつけ攻撃を開始する
	if (m_weaponState == EWeaponState::Ready)
	{
		m_smashAfterHolster = true;
		if (IsValid(m_currentGun)) { m_currentGun->StopFire(); }
		HolsterWeapon();
		return;
	}
	if (m_weaponState != EWeaponState::Holstered) { return; }
	m_smashAfterHolster = false;
	SetWeaponVisibility(false);
	//Blueprintや旧AIから呼ばれた場合も、見えていて衝撃の届く相手がいなければ跳躍を始めない
	AActor *target = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	AAIController *sight = Cast<AAIController>(GetController());
	if (!IsValid(target) || !sight || !sight->LineOfSightTo(target) ||
		FVector::Dist2D(GetActorLocation(), target->GetActorLocation()) > m_groundSmashRadius) { return; }
	//移動と射撃を停止し、踏み込み方向を予告時点で確定する
	if (AAIController *controller = Cast<AAIController>(GetController())) { controller->StopMovement(); }
	SetActionState(EActionState::Attacking);
	FaceTarget(target);
	//避けたプレイヤーへ空中で追従しないよう、短い踏み込みに制限する
	const FVector direction = target ? (target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D() : GetActorForwardVector();
	m_smashPending = true;
	if (GetMesh()->GetAnimInstance()->Montage_Play(m_montageMap.FindRef(TEXT("GroundSmash"))) <= 0.f)
	{
		//再生に失敗した場合も、アニメーションのないジャンプを開始しない
		m_smashPending = false;
		SetActionState(EActionState::Idle);
		return;
	}
	//溜めを見て離れる時間を確保してから跳躍する
	GetWorldTimerManager().SetTimer(m_smashTimer, FTimerDelegate::CreateWeakLambda(this, [this, direction]()
	{
		if (!m_smashPending || GetHealthRatio() <= 0.f || IsKnockedBack()) { return; }
		m_smashAirborne = true;
		LaunchCharacter(direction * 220.f + FVector(0.f, 0.f, 420.f), true, true);
	}), FMath::Clamp(m_groundSmashImpactDelay, 0.5f, 1.f), false);
}

//地面への接触を確認して跳躍攻撃の衝撃を一度だけ発生させる関数
void AEnemyChara::Landed(const FHitResult &_hit)
{
	Super::Landed(_hit);
	if (!m_smashPending || !m_smashAirborne) { return; }
	m_smashPending = false;
	m_smashAirborne = false;
	if (GetHealthRatio() <= 0.f || IsKnockedBack()) { return; }
	//再検索で天井や別の床を拾わず、着地通知の接触面を使う
	const FVector impactLocation = _hit.ImpactPoint + _hit.ImpactNormal * 2.f;

	//衝撃エフェクトのスケール値を計算する（参照半径が正の値の場合は比率を使用し、そうでない場合は1.0を使用する）
	const float effectScaleValue = m_groundSmashEffectReferenceRadius > 0.f
					   ? m_groundSmashRadius / m_groundSmashEffectReferenceRadius
					   : 1.f;
	//地面叩きつけEffectが設定されている場合だけ地面へ生成する
	if (m_smashEffect)
	{
		UNiagaraComponent *smashEffect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			GetWorld(), m_smashEffect, impactLocation,
			FRotationMatrix::MakeFromZ(_hit.ImpactNormal).Rotator(), FVector(effectScaleValue));
		ArmTransientEffectCleanup(smashEffect, 4.f);
	}

	//衝撃音を再生する（音量とピッチをランダム化する）
	if (m_groundSmashSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, m_groundSmashSound, impactLocation, 0.9f,
					  FMath::FRandRange(0.94f, 1.02f));
	}

	//衝撃ダメージを適用する（自身を無視するために ignoreActors に追加する）
	TArray<AActor *> ignoreActors;
	ignoreActors.Add(this);
	//近接支援に入った味方をボス自身の衝撃で倒さない
	for (TActorIterator<AEnemyChara> it(GetWorld()); it; ++it) { ignoreActors.Add(*it); }
	UGameplayStatics::ApplyRadialDamage(GetWorld(), GetPlayerAttackDamage(), impactLocation,
				m_groundSmashRadius, UDamageType::StaticClass(), ignoreActors, this,
				GetController(), true);
	//着地後は短い隙を残し、次の行動との重複を防ぐ
	GetWorldTimerManager().SetTimer(m_smashTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (GetHealthRatio() > 0.f && !IsKnockedBack()) { SetActionState(EActionState::Idle); }
	}), 0.45f, false);
}

//Barrage Shot を現在の攻撃対象へ実行する関数
void AEnemyChara::PerformBarrageShot()
{
	//現在の銃が有効でない場合やアクション状態が Idle でない場合は処理を中止する
	if (!IsValid(m_currentGun) || m_actionState != EActionState::Idle) { return; }

	//アクション状態を Attacking に設定する
	SetActionState(EActionState::Attacking);

	//一度の散弾攻撃で発射する弾数
	int32 pellets = 5;
	//散弾を左右へ広げる全体角度
	float spreadAngle = 45.0f;
	//隣り合う散弾の角度間隔
	float stepAngle = spreadAngle / (pellets - 1);
	//散弾を中央へ揃える最初の弾の角度
	float startAngle = -spreadAngle / 2.0f;

	//最終回転
	FRotator originalRotation = m_currentGun->GetActorRotation();

	//弾丸の数だけループして、各弾丸の回転を計算して発射する
	for (int32 i = 0; i < pellets; i++)
	{
		//射撃回転
		FRotator fireRotation = originalRotation;
		fireRotation.Yaw += startAngle + (stepAngle * i);

		//銃の回転を設定して発射する
		m_currentGun->SetActorRotation(fireRotation);
		m_currentGun->FireShot();
	}
	//銃の回転を元に戻す
	m_currentGun->SetActorRotation(originalRotation);
	SetActionState(EActionState::Idle);
}

//Teleport To 攻撃対象 を現在の攻撃対象へ実行する関数
void AEnemyChara::PerformTeleportToTarget()
{
	//現在の攻撃対象が有効でない場合や、すでにテレポート中の場合は処理を中止する
	if (m_isTeleporting) { return; }
	//レーザー攻撃中はTeleportで攻撃方向を変えない
	if (m_currentStyle == EEnemyAttackStyle::Laser && m_actionState == EActionState::Attacking) { return; }

	//プレイヤーのポーンを取得する
	AActor *player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	//プレイヤーが無効な場合は接近Teleportを中止する
	if (!player) { return; }

	//プレイヤーの位置を取得し、プレイヤーへの方向ベクトルを計算する
	FVector playerLoc = player->GetActorLocation();
	FVector dirToPlayer = (playerLoc - GetActorLocation()).GetSafeNormal2D();
	//プレイヤーの手前へ安全距離を空けた接近Teleport候補
	FVector destLoc = playerLoc - dirToPlayer * (GetMeleeStrikeRange(player) * 0.9f);

	//床と経路が確認できた候補だけを予約する
	FVector capturedDest;
	if (!FEnemyTeleportPlacement::Resolve(this, destLoc, capturedDest)) { return; }
	m_isTeleporting = true;
	if (AAIController *controller = Cast<AAIController>(GetController())) { controller->StopMovement(); }

	//地面の表面位置を取得するためのラムダ関数を定義する
	auto getGroundSurface = [&](FVector _inPos) -> FVector
	{
		//Teleport候補の真下にある床を取得する射線結果
		FHitResult hit;
		FVector traceStart = _inPos + FVector(0.f, 0.f, 500.f);
		FVector traceEnd = _inPos - FVector(0.f, 0.f, 1000.f);
		//敵自身を床判定から除外するCollision設定
		FCollisionQueryParams params;
		params.AddIgnoredActor(this);
		//床へ射線が当たった場合はTeleport先の高さを床面へ合わせる
		if (GetWorld()->LineTraceSingleByChannel(hit, traceStart, traceEnd, ECC_WorldStatic, params)) { return hit.Location; }
		return _inPos;
	};

	//テレポート開始エフェクトを再生する（存在する場合）
	if (m_teleportEffect)
	{
		//地面の表面位置を取得し、少し上にオフセットする
		FVector departGround = getGroundSurface(GetActorLocation()) + FVector(0.f, 0.f, 5.f);

		//テレポート開始エフェクトを指定位置にスポーンする
		UNiagaraComponent *departEffect =
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), m_teleportEffect, departGround, FRotator::ZeroRotator, FVector(1.f));
		ArmTransientEffectCleanup(departEffect, 0.65f);
	}

	//目的地の地面の表面位置を取得し、カプセルの半分の高さを考慮してキャプチャする
	const float capsuleHalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
	const FVector destinationGround = capturedDest - FVector(0.f, 0.f, capsuleHalfHeight + 2.f);
	//遅延実行後も同じ向きを使うために記録するプレイヤー方向
	const FVector capturedDir = dirToPlayer;
	const FVector capturedDestGround = destinationGround + FVector(0.f, 0.f, 5.f);

	//テレポート完了後の処理をタイマーで遅延実行するために、弱参照を作成する
	TWeakObjectPtr<AEnemyChara> weakThis(this);
	GetWorldTimerManager().SetTimer(
		m_teleportTimerHandle,
		[weakThis, capturedDest, capturedDir, capturedDestGround]()
		{
			//弱参照が有効でない場合は処理を中止する
			if (!weakThis.IsValid() || weakThis->GetHealthRatio() <= 0.f || !weakThis->m_isTeleporting || weakThis->IsKnockedBack()) { return; }

			//予告中に他の敵が入った場合も中止し、座標の自動補正で床外へ出ないようにする
			if (!FEnemyTeleportPlacement::HasSupport(weakThis.Get(), capturedDest) ||
				!weakThis->TeleportTo(capturedDest, weakThis->GetActorRotation(), false, true))
			{
				weakThis->m_isTeleporting = false;
				return;
			}
			weakThis->SetActorRotation(capturedDir.Rotation());
			weakThis->GetCharacterMovement()->StopMovementImmediately();

			//テレポート到着エフェクトを再生する（存在する場合）
			if (weakThis->m_teleportEffect)
			{
				UNiagaraComponent *arrivalEffect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
					weakThis->GetWorld(), weakThis->m_teleportEffect, capturedDestGround, FRotator::ZeroRotator, FVector(1.f));
				weakThis->ArmTransientEffectCleanup(arrivalEffect, 0.65f);
			}

			//到着演出後にTeleport中の状態を解除するTimer
			weakThis->GetWorldTimerManager().SetTimer(
				weakThis->m_teleportFinishTimer,
				[weakThis]()
				{
					//敵が有効な場合だけTeleport状態を解除する
					if (!weakThis.IsValid()) { return; }
					weakThis->m_isTeleporting = false;
					AActor *target = UGameplayStatics::GetPlayerPawn(weakThis->GetWorld(), 0);
					if (weakThis->GetHealthRatio() > 0.f && weakThis->m_currentStyle == EEnemyAttackStyle::Melee &&
						!weakThis->IsKnockedBack() && weakThis->CanCommitMeleeAttack(target)) { weakThis->PerformAttack(); }
				},
				0.2f, false);
		},
		0.45f, false);
}

//Teleport Away From 攻撃対象 を現在の攻撃対象へ実行する関数
void AEnemyChara::PerformTeleportAwayFromTarget()
{
	//現在の攻撃対象が有効でない場合や、すでにテレポート中の場合は処理を中止する
	if (m_isTeleporting) { return; }

	//レーザー攻撃中の場合は処理を中止する
	if (m_currentStyle == EEnemyAttackStyle::Laser && m_actionState == EActionState::Attacking) { return; }

	//プレイヤーのポーンを取得する
	AActor *player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	//プレイヤーが無効な場合は離脱Teleportを中止する
	if (!player) { return; }

	//プレイヤーの位置を取得し、プレイヤーから離れる方向ベクトルを計算する
	const FVector selfLoc = GetActorLocation();
	const FVector playerLoc = player->GetActorLocation();

	//プレイヤーから離れる方向ベクトルを計算し、目的地を設定する
	FVector dirAway = (selfLoc - playerLoc).GetSafeNormal2D();
	//プレイヤーから一定距離離れたTeleport候補
	FVector destLoc = playerLoc + dirAway * 1200.f;

	//退避方向が場外なら左右の候補を探し、安全な床がなければ移動しない
	FVector capturedDest;
	bool found = false;
	for (float angle : {0.f, 45.f, -45.f, 90.f, -90.f})
	{
		destLoc = playerLoc + dirAway.RotateAngleAxis(angle, FVector::UpVector) * 1200.f;
		if (FEnemyTeleportPlacement::Resolve(this, destLoc, capturedDest)) { found = true; break; }
	}
	if (!found) { return; }
	m_isTeleporting = true;
	if (AAIController *controller = Cast<AAIController>(GetController())) { controller->StopMovement(); }

	//地面の表面位置を取得するためのラムダ関数を定義する
	auto getGroundSurface = [&](FVector _inPos) -> FVector
	{
		//ラインキャストを使用して地面の表面位置を取得する
		FHitResult hit;
		FVector traceStart = _inPos + FVector(0.f, 0.f, 500.f);
		FVector traceEnd = _inPos - FVector(0.f, 0.f, 1000.f);
		//敵自身を床判定から除外するCollision設定
		FCollisionQueryParams params;
		params.AddIgnoredActor(this);
		//床へ射線が当たった場合はTeleport先の高さを床面へ合わせる
		if (GetWorld()->LineTraceSingleByChannel(hit, traceStart, traceEnd, ECC_WorldStatic, params)) { return hit.Location; }
		return _inPos;
	};

	//テレポート開始エフェクトを再生する（存在する場合）
	if (m_teleportEffect)
	{
		FVector departGround = getGroundSurface(selfLoc) + FVector(0.f, 0.f, 5.f);
		UNiagaraComponent *departEffect =
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), m_teleportEffect, departGround, FRotator::ZeroRotator, FVector(1.f));
		ArmTransientEffectCleanup(departEffect, 0.65f);
	}

	//目的地の地面の表面位置を取得し、カプセルの半分の高さを考慮してキャプチャする
	const float capsuleHalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 88.f;
	const FVector destinationGround = capturedDest - FVector(0.f, 0.f, capsuleHalfHeight + 2.f);
	const FVector capturedDestGround = destinationGround + FVector(0.f, 0.f, 5.f);

	//テレポート完了後の処理をタイマーで遅延実行するために、弱参照を作成する
	TWeakObjectPtr<AEnemyChara> weakThis(this);
	GetWorldTimerManager().SetTimer(
		m_teleportTimerHandle,
		[weakThis, capturedDest, capturedDestGround]()
		{
			//弱参照が有効でない場合は処理を中止する
			if (!weakThis.IsValid() || weakThis->GetHealthRatio() <= 0.f || !weakThis->m_isTeleporting || weakThis->IsKnockedBack()) { return; }
			//実行直前にも床と占有を確認し、安全を確認した座標だけへ移動する
			if (!FEnemyTeleportPlacement::HasSupport(weakThis.Get(), capturedDest) ||
				!weakThis->TeleportTo(capturedDest, weakThis->GetActorRotation(), false, true))
			{
				weakThis->m_isTeleporting = false;
				return;
			}

			//テレポート到着エフェクトを再生する（存在する場合）
			if (weakThis->m_teleportEffect)
			{
				UNiagaraComponent *arrivalEffect = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
					weakThis->GetWorld(), weakThis->m_teleportEffect, capturedDestGround, FRotator::ZeroRotator, FVector(1.f));
				weakThis->ArmTransientEffectCleanup(arrivalEffect, 0.65f);
			}

			//プレイヤーのポーンを取得し、存在する場合はプレイヤーの方向を向く
			weakThis->GetCharacterMovement()->StopMovementImmediately();
			AActor *currentPlayer = UGameplayStatics::GetPlayerPawn(weakThis->GetWorld(), 0);
			//到着時にもプレイヤーが有効な場合は正面を対象へ向ける
			if (currentPlayer)
			{
				FVector dirToPlayer = (currentPlayer->GetActorLocation() - capturedDest).GetSafeNormal2D();
				weakThis->SetActorRotation(dirToPlayer.Rotation());
			}

			//テレポート後のフラグをリセットするためのタイマーを設定する
			weakThis->GetWorldTimerManager().SetTimer(
				weakThis->m_teleportFinishTimer,
				[weakThis]()
				{
					//敵が有効な場合だけTeleport状態を解除する
					if (weakThis.IsValid()) { weakThis->m_isTeleporting = false; }
				},
				2.0f, false);
		},
		2.0f, false);
}

//LaserChargeを開始する関数
void AEnemyChara::StartLaserCharge()
{
	//収納中に遮蔽物へ隠れた相手は追尾せず、最後に確認した位置へ溜めを向ける
	AActor *target = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const AEnemyAIController *controller = Cast<AEnemyAIController>(GetController());
	if (controller && controller->CanObserveTarget(target)) { FaceTarget(target); }
	else if (controller && controller->HasCombatAwareness())
	{
		const FVector direction = controller->GetLastKnownPlayerLocation() - GetActorLocation();
		if (!direction.IsNearlyZero()) { SetActorRotation(FRotator(0.f, direction.Rotation().Yaw, 0.f)); }
	}

	//レーザー攻撃 Chargeのサウンドが有効な場合、指定位置で再生する（音量とピッチを指定する）
	if (m_bossLaserSound) { UGameplayStatics::PlaySoundAtLocation(this, m_bossLaserSound, GetActorLocation(), 0.58f, 0.68f); }

	//レーザー攻撃 Chargeのアニメーションを再生する（存在する場合）
	if (UAnimInstance *animInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr; animInstance && m_laserChargeAnimation)
	{
		animInstance->PlaySlotAnimationAsDynamicMontage(m_laserChargeAnimation, TEXT("DefaultSlot"), 0.12f, 0.12f, GetRangedAttackRate(), 1, -1.f, 0.f);
	}
	//レーザー攻撃 Chargeのモンタージュが有効な場合、モンタージュを再生する
	else if (m_montageMap.Contains(TEXT("LaserCharge")))
	{
		//レーザー溜めモンタージュを再生するAnimation Instance
		UAnimInstance *animInst = GetMesh()->GetAnimInstance();
		//Animation Instanceが有効な場合だけモンタージュを再生する
		if (animInst) { animInst->Montage_Play(m_montageMap[TEXT("LaserCharge")], GetRangedAttackRate()); }
	}

	//自動再生せずに生成し、読み込み待ちの再生要求を待機中へ持ち越さない
	if (!IsValid(m_laserChargeComp) && m_laserChargeGlowEffect && GetMesh())
	{
		const FName socket = GetMesh()->DoesSocketExist(TEXT("LaserMuzzle")) ? FName(TEXT("LaserMuzzle")) : NAME_None;
		m_laserChargeComp = UNiagaraFunctionLibrary::SpawnSystemAttached(m_laserChargeGlowEffect, GetMesh(), socket,
			FVector::ZeroVector, FRotator::ZeroRotator, EAttachLocation::SnapToTarget, false, false);
	}
	if (IsValid(m_laserChargeComp))
	{
		//ボスの拡大率を継承せず、溜めの光が画面全体を覆わない大きさに統一する
		m_laserChargeComp->SetAbsolute(false, false, true);
		m_laserChargeComp->SetWorldScale3D(FVector(0.12f));
		m_laserChargeComp->SetAutoActivate(false);
		m_laserChargeComp->Activate(true);
	}

	//レーザー攻撃
	//Chargeのマテリアルインスタンスを動的に作成し、EmissiveMultiplierパラメータを設定する
	for (int32 materialIndex = 0; materialIndex < GetMesh()->GetNumMaterials(); ++materialIndex)
	{
		//マテリアルインスタンスを動的に作成する
		UMaterialInstanceDynamic *dynamicMaterial = Cast<UMaterialInstanceDynamic>(GetMesh()->GetMaterial(materialIndex));
		//動的Materialでない場合は発光値を変更できるInstanceを作成する
		if (!dynamicMaterial) { dynamicMaterial = GetMesh()->CreateAndSetMaterialInstanceDynamic(materialIndex); }
		//動的Materialを取得できた場合は溜め中の発光を強める
		if (dynamicMaterial) { dynamicMaterial->SetScalarParameterValue(TEXT("EmissiveMultiplier"), 5.f); }
	}
}

//Laser攻撃Sequenceを開始する関数
bool AEnemyChara::IsLaserSequenceActive() const
{
	return GetWorld() && (m_smashAfterHolster || m_laserAfterHolster || GetWorld()->GetTimerManager().IsTimerActive(m_laserAttackTimerHandle) ||
		GetWorld()->GetTimerManager().IsTimerActive(m_laserStateResetTimerHandle));
}

//選択済みのレーザー攻撃を溜めから照射まで開始する関数
void AEnemyChara::BeginLaserAttackSequence()
{
	//レーザー攻撃がすでに実行中の場合や、ワールドが存在しない場合は処理を中止する
	if (m_actionState != EActionState::Idle || !GetWorld()) { return; }
	if (m_isSwitchingWeapon || m_isReloading || IsKnockedBack()) { return; }
	if (GetHealthRatio() <= 0.f) { m_laserAfterHolster = false; return; }
	//銃や近接武器を持ったまま溜め姿勢に入らず、収納通知から続行する
	if (m_weaponState == EWeaponState::Ready)
	{
		m_laserAfterHolster = true;
		StopFiring();
		if (AAIController *controller = Cast<AAIController>(GetController())) { controller->StopMovement(); }
		HolsterWeapon();
		return;
	}
	if (m_weaponState != EWeaponState::Holstered) { return; }
	m_laserAfterHolster = false;
	SetWeaponVisibility(false);

	//アクション状態を Attacking に設定する
	SetActionState(EActionState::Attacking);
	//レーザー溜め中はAI移動を止めて照準位置を安定させる
	if (AAIController *aiController = Cast<AAIController>(GetController())) { aiController->StopMovement(); }
	//レーザー攻撃 Chargeを開始する
	StartLaserCharge();

	//レーザー攻撃
	//Chargeのアニメーションが有効な場合、最終フレームと秒数を計算して、レーザー攻撃の持続時間を設定する
	if (m_laserChargeAnimation)
	{
		const int32 finalFrame = FMath::Max(m_laserChargeAnimation->GetNumberOfSampledKeys() - 1, 165);
		const float secondsPerFrame = m_laserChargeAnimation->GetPlayLength() / static_cast<float>(finalFrame);
		m_laserChargeDuration = secondsPerFrame * 120.f;
		m_laserEffectDuration = secondsPerFrame * 45.f;
	}

	//元の設定値を変更せず、再生速度に合わせた発射までの秒数を算出する
	const float chargeTime = m_laserChargeDuration / GetRangedAttackRate();
	//高速化後の発射時刻を使い、ジャスト回避の合図も同じだけ早める
	if (APlayerChara *player = Cast<APlayerChara>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0)))
	{
		player->SchedulePerfectDodgeForLaser(this, chargeTime);
	}

	//レーザー攻撃 Chargeの持続時間後に ExecuteChargedLaserAttack を呼び出すタイマーを設定する
	GetWorldTimerManager().SetTimer(m_laserAttackTimerHandle, this, &AEnemyChara::ExecuteChargedLaserAttack, chargeTime, false);
}

//Charged レーザー攻撃を現在の攻撃対象へ実行する関数
void AEnemyChara::ExecuteChargedLaserAttack()
{
	//溜め中に倒された敵からレーザーを発射しない
	if (GetHealthRatio() <= 0.f) { StopLaserCharge(); return; }
	//レーザー攻撃 Chargeを終了する
	AActor *currentTarget = GetWorld() ? UGameplayStatics::GetPlayerPawn(GetWorld(), 0) : nullptr;

	//現在の攻撃対象が有効でない場合、レーザー攻撃 Chargeを停止し、アクション状態を Idle
	//に設定して処理を終了する
	if (!IsValid(currentTarget))
	{
		StopLaserCharge();
		SetActionState(EActionState::Idle);
		return;
	}
	//視認できる時だけ現在位置へ向き、見失った時の照射先は最後の目撃位置に任せる
	const AEnemyAIController *controller = Cast<AEnemyAIController>(GetController());
	if (controller && controller->CanObserveTarget(currentTarget)) { FaceTarget(currentTarget); }
	LaserAttack();
}

//レーザー攻撃 Chargeを終了し、専用タイマーと一時フラグを解除する関数
void AEnemyChara::StopLaserCharge()
{
	//レーザー攻撃 Chargeのタイマーをクリアする
	if (IsValid(m_laserChargeComp))
	{
		//停止だけでなく破棄し、非同期読み込み後に溜めの光が再開しないようにする
		m_laserChargeComp->DeactivateImmediate();
		m_laserChargeComp->DestroyComponent();
	}
	m_laserChargeComp = nullptr;

	//レーザー攻撃 Chargeのマテリアルインスタンスの EmissiveMultiplierパラメータをリセットする
	for (int32 i = 0; i < GetMesh()->GetNumMaterials(); i++)
	{
		//通常の発光値へ戻す対象Material
		UMaterialInstanceDynamic *dynMat = Cast<UMaterialInstanceDynamic>(GetMesh()->GetMaterial(i));
		//動的Materialの場合だけ発光Parameterを変更する
		if (dynMat) { dynMat->SetScalarParameterValue(TEXT("EmissiveMultiplier"), 1.f); }
	}
}
