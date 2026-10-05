#include "Enemy/EnemyChara.h"
#include "Combat/MeleeComponent.h"
#include "Movement/CharacterPace.h"
#include "TimerManager.h"
#include "GameFramework/DamageType.h"
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
#include "Enemy/Components/EnemyHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Sound/SoundBase.h"
#include "Audio/FootstepAttenuation.h"
#include "UnrealClient.h"
#include "Enemy/Components/EnemyCoverComponent.h"

//最低硬直時間と着地を確認し、落下し続けても二秒で制御を戻す関数
bool AEnemyChara::IsKnockedBack() const
{
	if (!GetWorld() || m_knockbackUntil < 0.f) { return false; }
	const float time = GetWorld()->GetTimeSeconds();
	const UCharacterMovementComponent *movement = GetCharacterMovement();
	return time < m_knockbackUntil || (time < m_knockbackUntil + 1.5f && movement && movement->IsFalling());
}

//瞬間移動を使わず、衝突を伴う移動として敵を吹き飛ばす関数
void AEnemyChara::ApplyKnockback(const FVector &_velocity)
{
	if (!GetWorld() || GetHealthRatio() <= 0.f || !GetCharacterMovement()) { return; }
	//攻撃予約と遮蔽物への移動を解除して吹き飛びと競合させない
	RecoverFromStalledAction();
	GetWorldTimerManager().ClearTimer(m_attackCoolDown);
	if (m_coverComponent) { m_coverComponent->FinishCoverUse(); }
	if (AAIController *controller = Cast<AAIController>(GetController())) { controller->StopMovement(); }
	if (UAnimInstance *animation = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr) { animation->Montage_Stop(0.1f); }
	m_knockbackUntil = GetWorld()->GetTimeSeconds() + 0.5f;
	SetActionState(EActionState::Stunned);
	GetCharacterMovement()->StopMovementImmediately();
	LaunchCharacter(_velocity, true, true);
}

//攻撃 を現在の攻撃対象へ実行する関数
void AEnemyChara::PerformAttack()
{
	if (IsKnockedBack() || GetHealthRatio() <= 0.f) { return; }
	//攻撃中、リロード中、武器切替中、武器が準備できていない場合は攻撃を実行しない。
	if (m_actionState == EActionState::Attacking || m_actionState == EActionState::Reloading) { return; }

	if (m_isSwitchingWeapon) { return; }

	if (m_weaponState != EWeaponState::Ready) { return; }

	//攻撃対象を取得する
	AActor *target = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

	if (m_currentStyle == EEnemyAttackStyle::Melee) FaceTarget(target);

	//現在値が有効な場合だけ攻撃可能距離に合う攻撃を実行する
	if (m_currentStyle == EEnemyAttackStyle::Melee && !CanCommitMeleeAttack(target)) { return; }

	//攻撃対象を向く
	FaceTarget(target);
	SetActionState(EActionState::Attacking);

	//攻撃スタイルに応じて攻撃を実行する
	switch (m_currentStyle)
	{
	case EEnemyAttackStyle::Melee:
		MeleeAttack();
		break;
	case EEnemyAttackStyle::Gun:
		GunAttack();
		break;
	case EEnemyAttackStyle::Laser:
		LaserAttack();
		break;
	}
}

//敵銃の残弾不足を検知してリロードモンタージュと補充待機状態を開始する関数
void AEnemyChara::PeformReload()
{
	if (IsKnockedBack() || GetHealthRatio() <= 0.f) { return; }
	//リロード条件を満たしていない場合はリロードを実行しない。
	if (!IsValid(m_currentGun)) { return; }

	//リロードの弾薬状態に合わせて射撃または補充処理へ分岐する
	if (m_isReloading) { return; }

	if (!m_currentGun->IsOutOfAmmo() && m_currentGun->GetCurrentAmmo() >= FMath::Max(1, m_currentGun->GetClipSize() / 4)) { return; }
	//現在値の弾薬状態に合わせて射撃または補充処理へ分岐する
	if (m_currentGun->GetTotalAmmo() <= 0) { return; }

	//リロードを開始する
	m_currentGun->StopFire();
	m_isReloading = true;
	SetActionState(EActionState::Reloading);

	//リロード音を再生する
	if (m_weaponHandlingSound) { UGameplayStatics::PlaySoundAtLocation(this, m_weaponHandlingSound, GetActorLocation(), 0.5f, 0.72f); }

	//リロードモンタージュを再生する
	UAnimMontage *reloadMontage = nullptr;
	//Montageの弾薬状態に合わせて射撃または補充処理へ分岐する
	if (m_montageMap.Contains(TEXT("Reload"))) { reloadMontage = m_montageMap[TEXT("Reload")]; }

	//リロードモンタージュの再生時間を取得する
	float reloadDuration = 2.0f;
	//Montageの弾薬状態に合わせて射撃または補充処理へ分岐する
	if (reloadMontage)
	{
		UAnimInstance *animInst = GetMesh()->GetAnimInstance();
		//別グループの脚の走行は止めず、上半身で補充動作を再生する
		if (animInst) { reloadDuration = animInst->Montage_Play(reloadMontage, 1.f, EMontagePlayReturnType::MontageLength, 0.f, false); }
	}

	//リロードタイマーを設定する
	GetWorldTimerManager().ClearTimer(m_reloadTimerHandle);
	GetWorldTimerManager().SetTimer(m_reloadTimerHandle, this, &AEnemyChara::FinishReload, FMath::Max(reloadDuration, 0.1f), false);
}

//攻撃 モンタージュを対象メッシュまたは音響コンポーネントで再生する関数
void AEnemyChara::PlayAttackMontage()
{
	//攻撃モンタージュを再生する
	UAnimInstance *animInst = GetMesh()->GetAnimInstance();

	if (!animInst || !m_attackMontage)
	{
		SetActionState(EActionState::Idle);
		return;
	}

	//攻撃モンタージュが再生中でない場合は、攻撃モンタージュを再生する
	if (!animInst->Montage_IsPlaying(m_attackMontage))
	{
		m_currentAttackIndex = 0;
		float duration = animInst->Montage_Play(m_attackMontage);

		//攻撃モンタージュの最初のセクションにジャンプする
		if (m_attackMontage->IsValidSectionName(TEXT("Attack1"))) { animInst->Montage_JumpToSection(TEXT("Attack1"), m_attackMontage); }

		//攻撃モンタージュの再生時間が1秒以上の場合は、攻撃モンタージュの再生終了後にデフォルト速度に戻すタイマーを設定する
		FTimerHandle timerHandle;
		TWeakObjectPtr<AEnemyChara> weakThis(this);
		GetWorld()->GetTimerManager().SetTimer(
			timerHandle,
			[weakThis]()
			{
				if (weakThis.IsValid() && weakThis->m_actionState == EActionState::Attacking)
				{
					weakThis->RestoreDefaultSpeed();
					weakThis->SetActionState(EActionState::Idle);
				}
			},
			FMath::Max(duration, 1.0f), false);
	}
}

//近接攻撃 攻撃に対応するクラス状態を更新する関数
void AEnemyChara::MeleeAttack()
{
	//近接攻撃モンタージュを取得する
	UAnimMontage *meleeMontage = nullptr;

	if (m_montageMap.Contains(TEXT("Melee"))) { meleeMontage = m_montageMap[TEXT("Melee")]; }

	//近接攻撃モンタージュが存在する場合は、近接攻撃モンタージュを再生する
	if (meleeMontage)
	{
		//振りかぶり開始では鳴らさず、実際に剣を振る受付区間まで待つ
		m_meleeSwingPlayed = false;

		//近接攻撃モンタージュを再生する
		m_attackMontage = meleeMontage;
		PlayAttackMontage();

		//近接攻撃 命中 受付時間を開始する
		CloseMeleeHitWindow();
		const float montageDuration = FMath::Max(meleeMontage->GetPlayLength(), 0.2f);
		GetWorldTimerManager().SetTimer(m_meleeHitWindowOpenTimerHandle, this, &AEnemyChara::OpenMeleeHitWindow,
										montageDuration * m_meleeHitWindowStartRatio, false);
		GetWorldTimerManager().SetTimer(m_meleeHitWindowCloseTimerHandle, this, &AEnemyChara::CloseMeleeHitWindow,
										montageDuration * FMath::Max(m_meleeHitWindowEndRatio, m_meleeHitWindowStartRatio + 0.05f), false);
	}
	else { SetActionState(EActionState::Idle); }
}

//照準完了と射線を確認して敵のバースト射撃を開始する関数
void AEnemyChara::GunAttack()
{
	//銃が有効でない場合は、攻撃を中止する
	if (!IsValid(m_currentGun))
	{
		SetActionState(EActionState::Idle);
		return;
	}

	//銃の弾薬が不足している場合は、リロードを実行する
	if (m_currentGun->IsOutOfAmmo() && m_currentGun->GetTotalAmmo() <= 0)
	{
		if (m_enemyRank == EEnemyRank::MiddleBoss || m_enemyRank == EEnemyRank::LastBoss) { SwitchWeapon(EEnemyAttackStyle::Melee); }
		else
		{
			m_currentStyle = EEnemyAttackStyle::Melee;
			SetWeaponVisibility(false);
		}
		//銃の弾薬が不足している場合は、攻撃を中止する
		SetActionState(EActionState::Idle);
		return;
	}

	//銃の弾薬が不足している場合は、リロードを実行する
	if (m_currentGun->IsOutOfAmmo())
	{
		//リロードの弾薬状態に合わせて射撃または補充処理へ分岐する
		if (m_actionState == EActionState::Reloading) { return; }
		PeformReload();
		return;
	}

	//銃が発射可能でない場合は、攻撃を中止する
	if (!m_currentGun->CanFire())
	{
		SetActionState(EActionState::Idle);
		return;
	}
	//銃の発射を開始する
	UAnimMontage *gunMontage = nullptr;
	if (m_montageMap.Contains(TEXT("Gun"))) { gunMontage = m_montageMap[TEXT("Gun")]; }

	//銃の発射モンタージュが存在する場合は、銃の発射モンタージュを再生する
	if (gunMontage)
	{
		m_attackMontage = gunMontage;
		UAnimInstance *animation = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
		if (!animation || animation->Montage_Play(gunMontage) <= 0.f)
		{
			SetActionState(EActionState::Idle);
			return;
		}
		//構え上げは先頭から再生し、照準姿勢に入った後だけ射撃区間を繰り返す
		if (gunMontage->IsValidSectionName(TEXT("Fire")))
		{
			animation->Montage_SetNextSection(TEXT("Fire"), TEXT("Fire"), gunMontage);
		}
	}
	else { BeginFireSequence(); }
}

//レーザー攻撃 攻撃に対応するクラス状態を更新する関数
void AEnemyChara::LaserAttack()
{
	//レーザー攻撃のチャージを停止する
	StopLaserCharge();

	//ワールドを取得する
	UWorld *world = GetWorld();

	if (!world)
	{
		SetActionState(EActionState::Idle);
		return;
	}

	//レーザー攻撃の音を再生する
	if (m_bossLaserSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, m_bossLaserSound, GetActorLocation(), 1.0f, FMath::FRandRange(0.97f, 1.03f));
	}

	//レーザー攻撃のモンタージュを再生する
	if (!m_laserChargeAnimation && m_montageMap.Contains(TEXT("Laser")))
	{
		UAnimInstance *animInst = GetMesh()->GetAnimInstance();

		if (animInst) { animInst->Montage_Play(m_montageMap[TEXT("Laser")]); }
	}

	//レーザー攻撃の対象を取得する
	AActor *target = UGameplayStatics::GetPlayerPawn(world, 0);

	if (!IsValid(target))
	{
		SetActionState(EActionState::Idle);
		return;
	}

	//レーザー攻撃の対象のカプセルコンポーネントを取得する
	const UCapsuleComponent *targetCapsule = target->FindComponentByClass<UCapsuleComponent>();
	FVector targetPosition = targetCapsule ? targetCapsule->GetComponentLocation() : target->GetActorLocation();
	//溜め中に隠れた相手は最後の目撃位置へ照射し、壁の向こうの現在位置を追尾しない
	if (const AEnemyAIController *controller = Cast<AEnemyAIController>(GetController()); controller && !controller->CanObserveTarget(target))
	{
		targetPosition = controller->GetLastKnownPlayerLocation();
	}
	const FVector actorToTarget = (targetPosition - GetActorLocation()).GetSafeNormal2D();

	//レーザー攻撃の対象がほぼ同じ位置にある場合は、攻撃を中止する
	if (actorToTarget.IsNearlyZero())
	{
		SetActionState(EActionState::Idle);
		return;
	}

	//ボス回転
	FRotator bossRotation = actorToTarget.Rotation();
	bossRotation.Pitch = 0.f;
	bossRotation.Roll = 0.f;
	SetActorRotation(bossRotation);

	//ボスの回転後に銃口位置を取得してレーザー方向のずれを防ぐ処理
	FVector start = GetActorLocation() + GetActorForwardVector() * 100.f + FVector(0.f, 0.f, 80.f);

	if (GetMesh() && GetMesh()->DoesSocketExist(TEXT("LaserMuzzle")))
	{
		const FVector socketLocation = GetMesh()->GetSocketLocation(TEXT("LaserMuzzle"));

		//Socketとの距離が行動可能範囲内か確認する
		if (FVector::DistSquared(socketLocation, GetActorLocation()) <= FMath::Square(500.f)) { start = socketLocation; }
	}

	//現在位置からターゲットへ向かう攻撃方向
	const FVector direction = (targetPosition - start).GetSafeNormal();

	if (direction.IsNearlyZero() || direction.ContainsNaN())
	{
		SetActionState(EActionState::Idle);
		return;
	}

	//レーザー攻撃の終点を設定する
	FVector end = start + direction * m_laserRange;

	//レーザー攻撃の衝突判定を行い、ヒットした場合は終点をヒット位置に設定する
	FHitResult hitResult;
	FCollisionQueryParams queryParams(SCENE_QUERY_STAT(BossLaser), false);
	queryParams.AddIgnoredActor(this);
	const FCollisionShape beamShape = FCollisionShape::MakeSphere(m_laserBeamRadius);

	if (world->SweepSingleByChannel(hitResult, start, end, FQuat::Identity, ECC_Visibility, beamShape, queryParams)) { end = hitResult.ImpactPoint; }

	//レーザー攻撃の対象がレーザーの範囲内にいるかどうかを判定する
	const float visibleBeamLength = FVector::Distance(start, end);

	//照準に使った記憶位置ではなく、現在の体が実際にビームへ触れたかを判定する
	const FVector targetCenter = targetCapsule ? targetCapsule->GetComponentLocation() : target->GetActorLocation();

	//レーザー攻撃の対象がレーザーの範囲内にいるかどうかを判定する
	const float targetAlongBeam = FVector::DotProduct(targetCenter - start, direction);
	const FVector closestBeamPoint = start + direction * FMath::Clamp(targetAlongBeam, 0.f, visibleBeamLength);

	//レーザー攻撃の対象の衝突半径を取得する
	const float targetCollisionRadius =
		targetCapsule ? FMath::Max(targetCapsule->GetScaledCapsuleRadius(), 1.f) : FMath::Max(target->GetSimpleCollisionRadius(), 1.f);
	const bool damagedTarget = targetAlongBeam >= -targetCollisionRadius && targetAlongBeam <= visibleBeamLength + targetCollisionRadius &&
								 FVector::DistSquared(targetCenter, closestBeamPoint) <= FMath::Square(m_laserBeamRadius + targetCollisionRadius);

	//レーザー攻撃の対象がダメージを受ける場合は、ダメージを適用する
	if (damagedTarget) { UGameplayStatics::ApplyDamage(target, GetPlayerAttackDamage(), GetController(), this, UDamageType::StaticClass()); }

	//レーザー攻撃の方向を回転に変換する
	const FRotator laserRotation = direction.Rotation();
	world->GetTimerManager().SetTimer(m_laserStateResetTimerHandle, this, &AEnemyChara::FinishLaserAttack, m_laserEffectDuration, false);

	//レーザー攻撃のエフェクトを生成する
	if (m_laserEffect)
	{
		CleanupLaserEffect();
		m_activeLaserBeam = UNiagaraFunctionLibrary::SpawnSystemAtLocation(world, m_laserEffect, start, laserRotation, FVector(1.f), false, false,
																		   ENCPoolMethod::None, true);

		//レーザー攻撃のエフェクトの終点を設定する
		if (IsValid(m_activeLaserBeam))
		{
			//レーザー攻撃のエフェクトの終点を設定する
			m_activeLaserBeam->SetVariableVec3(FName("User.Beam End"), end);
			m_activeLaserBeam->Activate(true);

			//レーザー攻撃のエフェクトのクリーンアップタイマーを設定する
			world->GetTimerManager().SetTimer(m_laserEffectCleanupTimerHandle, this, &AEnemyChara::CleanupLaserEffect, m_laserEffectDuration, false);
		}
	}
}

//レーザー攻撃を完了する関数
void AEnemyChara::FinishLaserAttack()
{
	if (m_actionState == EActionState::Attacking) { SetActionState(EActionState::Idle); }
}

//Laser終了時にBeam、予兆Effect、Timerをまとめて解除する関数
void AEnemyChara::CleanupLaserEffect()
{
	//レーザー攻撃のエフェクトを破棄する
	if (UWorld *world = GetWorld()) { world->GetTimerManager().ClearTimer(m_laserEffectCleanupTimerHandle); }

	//レーザー攻撃のエフェクトを破棄する
	if (IsValid(m_activeLaserBeam))
	{
		m_activeLaserBeam->DeactivateImmediate();
		m_activeLaserBeam->DestroyComponent();
	}
	//レーザー攻撃のエフェクトの参照をクリアする
	m_activeLaserBeam = nullptr;
}

//近距離Animationの命中Frameに合わせて武器判定を有効化する関数
void AEnemyChara::OpenMeleeHitWindow()
{
	if (IsKnockedBack() || GetHealthRatio() <= 0.f) { return; }
	//近接攻撃 命中 受付時間を開始する条件を満たしていない場合は、処理を終了する
	if (m_actionState != EActionState::Attacking || m_currentStyle != EEnemyAttackStyle::Melee) { return; }

	if (m_meleeHitWindowOpen) { return; }
	//空振りでも風切り音を鳴らし、離れた敵の音は足音と同じ距離減衰で小さくする
	if (!m_meleeSwingPlayed)
	{
		m_meleeSwingPlayed = true;
		USoundBase *swing = FMath::RandBool() ? m_meleeSwingSoundA.Get() : m_meleeSwingSoundB.Get();
		if (!swing) { swing = m_meleeSwingSoundA ? m_meleeSwingSoundA.Get() : m_meleeSwingSoundB.Get(); }
		if (swing)
		{
			UGameplayStatics::PlaySoundAtLocation(this, swing, GetActorLocation(), 0.88f,
				FMath::FRandRange(0.94f, 1.06f), 0.f, GetMutableDefault<UFootstepAttenuation>());
		}
	}

	//近接攻撃 命中 受付時間を開始する
	AActor *target = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);

	if (!IsTargetWithinMeleeStrikeRange(target))
	{
		m_attackHit = false;
		return;
	}

	//近接攻撃 命中 受付時間を開始する
	FaceTarget(target);
	m_meleeHitWindowOpen = true;

	//近接攻撃の武器のダメージを設定し、武器を有効化する
	if (AMeleeWeapon *weaponActor = Cast<AMeleeWeapon>(m_meleeWeapon))
	{
		weaponActor->SetDamage(GetPlayerAttackDamage());
		weaponActor->ActivateWeapon();
	}
}

//近接攻撃 命中 受付時間を終了し、専用タイマーと一時フラグを解除する関数
void AEnemyChara::CloseMeleeHitWindow()
{
	//近接攻撃 命中 受付時間を終了する条件を満たしていない場合は、処理を終了する
	GetWorldTimerManager().ClearTimer(m_meleeHitWindowOpenTimerHandle);

	//命中が有効な場合だけ攻撃可能距離に合う攻撃を実行する
	if (!m_meleeHitWindowOpen) { return; }

	//近接攻撃 命中 受付時間を終了する
	m_meleeHitWindowOpen = false;

	if (AMeleeWeapon *weaponActor = Cast<AMeleeWeapon>(m_meleeWeapon)) { weaponActor->DeactivateWeapon(); }
}

//ラストボスの召喚上限を守りながら援護用の雑魚敵を戦闘空間へ生成する関数
void AEnemyChara::SummonMinions()
{
	//召喚可能な雑魚敵クラスが存在しない場合は、処理を終了する
	if (m_minionClass.Num() == 0) { return; }

	if (m_hasSummoned) { return; }
	m_hasSummoned = true;

	//召喚モンタージュが存在する場合は、召喚モンタージュを再生する
	bool bPlayedAnim = false;

	if (m_montageMap.Contains(TEXT("Summon")))
	{
		//召喚モンタージュを再生する
		UAnimInstance *animInst = GetMesh()->GetAnimInstance();

		if (animInst)
		{
			//召喚モンタージュの再生時間を取得する
			float animDuration = animInst->Montage_Play(m_montageMap[TEXT("Summon")]);

			if (animDuration > 0.f)
			{
				bPlayedAnim = true;

				//召喚モンタージュの再生時間の半分後に、召喚処理を実行するタイマーを設定する
				GetWorldTimerManager().SetTimer(m_summonTimer, this, &AEnemyChara::ExecuteSummon, animDuration * 0.5f, false);
			}
		}
	}

	//召喚モンタージュが存在しない場合は、即座に召喚処理を実行する
	if (!bPlayedAnim) { ExecuteSummon(); }
}

//Summon を現在の攻撃対象へ実行する関数
void AEnemyChara::ExecuteSummon()
{
	//死体が残る間や吹き飛び中の通知から援軍を生成しない
	if (GetHealthRatio() <= 0.f || IsKnockedBack() || IsHidden()) { return; }
	//召喚する雑魚敵の数を設定する
	constexpr int32 summonCount = 1;

	//召喚する雑魚敵の数だけループして、雑魚敵を生成する
	for (int32 i = 0; i < summonCount; i++)
	{
		//召喚位置を設定する
		FVector spawnPos = GetActorLocation();
		UNavigationSystemV1 *navSys = UNavigationSystemV1::GetCurrent(GetWorld());

		//ナビゲーションシステムが存在する場合は、ランダムな到達可能な位置を取得する
		if (navSys)
		{
			//担当機能の状態を更新するために使用するrandomLoc
			FNavLocation randomLoc;

			if (navSys->GetRandomReachablePointInRadius(GetActorLocation(), 500.f, randomLoc)) { spawnPos = randomLoc.Location; }
			else { spawnPos += FVector(FMath::RandRange(-400.f, 400.f), FMath::RandRange(-400.f, 400.f), 0.f); }
		}
		spawnPos.Z += 80.f;

		//召喚する雑魚敵のクラスを設定する
		FName rowName = (FMath::RandBool()) ? FName("Zako_Melee") : FName("Zako_Gun");

		//召喚する雑魚敵のクラスをデータテーブルから取得する
		TSubclassOf<AEnemyChara> minionClass = nullptr;

		if (m_enemyData)
		{
			//データテーブルから雑魚敵のクラスを取得する
			static const FString contextString(TEXT("Summon Spawn Lookup"));

			//DataTable行へ安全にアクセスする参照
			FEnemyData *row = m_enemyData->FindRow<FEnemyData>(rowName, contextString);

			if (row && row->m_enemyBpClass) { minionClass = row->m_enemyBpClass; }
		}

		//雑魚敵のクラスが取得できない場合は、ランダムに雑魚敵のクラスを選択する
		if (!minionClass && m_minionClass.Num() > 0)
		{
			int32 classIndex = FMath::RandRange(0, m_minionClass.Num() - 1);
			minionClass = m_minionClass[classIndex];
		}

		//雑魚敵のクラスが取得できない場合は、処理をスキップする
		if (!minionClass) { continue; }

		//雑魚敵を生成する
		AEnemyChara *minion = GetWorld()->SpawnActorDeferred<AEnemyChara>(minionClass, FTransform(FRotator::ZeroRotator, spawnPos), nullptr, nullptr,
																		  ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);

		//雑魚敵が生成できない場合は、処理をスキップする
		if (!minion) { continue; }

		//雑魚敵のランクを設定し、データテーブルと行名を設定する
		minion->m_enemyRank = EEnemyRank::Minion;
		minion->m_enemyData = m_enemyData;
		minion->m_enemyRowName = rowName;
		minion->Tags.AddUnique(TEXT("Enemy"));

		//雑魚敵のタグを設定する
		for (const FName &ownerTag : Tags)
		{
			if (ownerTag.ToString().StartsWith(TEXT("CombatRoom_"))) { minion->Tags.AddUnique(ownerTag); }
		}

		//雑魚敵のAIコントローラーを設定する
		minion->AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
		minion->AIControllerClass = AEnemyAIController::StaticClass();

		//設定済みの座標情報でアクター生成を完了する処理
		UGameplayStatics::FinishSpawningActor(minion, FTransform(FRotator::ZeroRotator, spawnPos));

		//雑魚敵のAIコントローラーを生成する
		if (!minion->GetController()) { minion->SpawnDefaultController(); }

		//召喚敵にも全体の減速を適用し、近接型には前回の15％減速も維持する
		if (minion->GetCharacterMovement())
		{
			minion->m_defaultMoveSpeed = 400.f * CharacterPace::WalkScale * (minion->m_currentStyle == EEnemyAttackStyle::Melee ? m_walkScale : 1.f);
			minion->GetCharacterMovement()->MaxWalkSpeed = minion->m_defaultMoveSpeed;
		}

		//召喚エフェクトを生成する
		if (m_summonEffect)
		{
			UNiagaraComponent *summonEffect =
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), m_summonEffect, spawnPos, FRotator::ZeroRotator, FVector(1.f));

			ArmTransientEffectCleanup(summonEffect, 1.0f);
		}
	}
}

//攻撃命中の通知を受け取る関数
void AEnemyChara::OnAttackHit()
{
	//攻撃が命中した場合の処理を実行する
	if (m_currentStyle == EEnemyAttackStyle::Melee)
	{
		m_attackHit = true;

		//近接攻撃 命中 受付時間を開始する
		OpenMeleeHitWindow();

		if (!IsValid(m_meleeWeapon) && m_meleeComp) { m_meleeComp->MeleeAttack(); }
	}
	//銃 攻撃が命中した場合の処理を実行する
	else if (m_currentStyle == EEnemyAttackStyle::Gun) { GunAttack(); }
}

//近接攻撃の命中対象へ敵ランク別ダメージを適用し、重複命中を防止する関数
void AEnemyChara::TakeDamageMelee(int _damage, AActor *_damageCauser)
{
	UGameplayStatics::ApplyDamage(this, _damage, _damageCauser ? _damageCauser->GetInstigatorController() : nullptr, _damageCauser,
								  UDamageType::StaticClass());
}

//攻撃状態を初期状態へ戻す関数
void AEnemyChara::ResetAttackState()
{
	m_isAttacking = false;
}

//攻撃Rangeの現在値を参照側へ渡す関数
float AEnemyChara::GetAttackRange() const
{
	return m_attackRange;
}

//武器の実射程を超える設定値で接近を止めないための攻撃距離を返す関数
float AEnemyChara::GetEffectiveAttackRange(const AActor *_target) const
{
	if (m_currentStyle == EEnemyAttackStyle::Melee) { return GetMeleeStrikeRange(_target); }
	if (m_currentStyle == EEnemyAttackStyle::Laser) { return m_enemyRank == EEnemyRank::Minion ? 1800.f : 2500.f; }
	//銃未装備なら射撃待機せず接近し、装備済みなら銃自体の射程を使う
	if (m_currentStyle == EEnemyAttackStyle::Gun) { return GetCurrentGun() ? GetCurrentGun()->GetFireRange() : 0.f; }
	return GetAttackRange();
}

//MeleeStrikeRangeの現在値を参照側へ渡す関数
float AEnemyChara::GetMeleeStrikeRange(const AActor *_target) const
{
	//近接攻撃 Strike 攻撃範囲を計算する
	const float selfRadius = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleRadius() : 42.f;

	//攻撃対象へ安全にアクセスする参照
	const UCapsuleComponent *targetCapsule = _target ? _target->FindComponentByClass<UCapsuleComponent>() : nullptr;
	const float targetRadius = targetCapsule ? targetCapsule->GetScaledCapsuleRadius() : 42.f;

	//担当機能の状態を更新するために使用する武器
	const float physicalWeaponReach = 130.f;

	//担当機能の状態を更新するために使用するmaximumRange
	const float maximumRange = m_enemyRank == EEnemyRank::Minion ? 260.f : 500.f;

	//近接攻撃 Strike 攻撃範囲を返す
	return FMath::Clamp(selfRadius + targetRadius + physicalWeaponReach, 170.f, maximumRange);
}

//攻撃対象 Within 近接攻撃 Strike 攻撃範囲が現在成立しているかを判定する関数
bool AEnemyChara::IsTargetWithinMeleeStrikeRange(const AActor *_target) const
{
	return CanReachMeleeHeight(_target) && FVector::DistSquared2D(GetActorLocation(), _target->GetActorLocation()) <= FMath::Square(GetMeleeStrikeRange(_target));
}

bool AEnemyChara::CanReachMeleeHeight(const AActor *_target) const
{
	if (!_target) { return false; }
	//中心の立体距離ではなく、横方向の間合いと体の高さを分けて確認する
	const UCapsuleComponent *targetBody = _target->FindComponentByClass<UCapsuleComponent>();
	const float selfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float targetHeight = targetBody ? targetBody->GetScaledCapsuleHalfHeight() : 42.f;
	return FMath::Abs(GetActorLocation().Z - _target->GetActorLocation().Z) <= selfHeight + targetHeight;
}

//近接攻撃 攻撃が現在成立しているかを判定する関数
bool AEnemyChara::CanCommitMeleeAttack(const AActor *_target) const
{
	//攻撃対象が有効な場合だけ攻撃可能距離に合う攻撃を実行する
	if (!IsTargetWithinMeleeStrikeRange(_target)) { return false; }

	const float strikeRange = GetMeleeStrikeRange(_target);
	const float currentDistance = FVector::Dist2D(GetActorLocation(), _target->GetActorLocation());

	//対象へ安全にアクセスする参照
	const float commitmentRange = strikeRange * (m_enemyRank == EEnemyRank::Minion ? 0.92f : 1.f);

	//現在値との距離が行動可能範囲内か確認する
	if (currentDistance > commitmentRange) { return false; }

	//近接攻撃の予測位置を計算する
	const float windupPrediction = m_enemyRank == EEnemyRank::Minion ? 0.28f : 0.38f;
	const FVector predictedTarget = _target->GetActorLocation() + _target->GetVelocity() * windupPrediction;
	const float predictedDistance = FVector::Dist2D(GetActorLocation(), predictedTarget);

	//距離との距離が行動可能範囲内か確認する
	if (predictedDistance > strikeRange * 1.05f) { return false; }

	//近接攻撃の方向を計算する
	const FVector toTarget = (_target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	return FVector::DotProduct(GetActorForwardVector().GetSafeNormal2D(), toTarget) >= 0.72f;
}

//攻撃モンタージュ終了時に攻撃状態と命中判定を解除する関数
void AEnemyChara::OnAttackEnd()
{
	//死亡後や別の攻撃中に届いた通知から近接コンボを再開しない
	if (GetHealthRatio() <= 0.f || m_actionState != EActionState::Attacking) { return; }
	//近接攻撃 命中 受付時間を終了する
	CloseMeleeHitWindow();
	UAnimInstance *animInst = GetMesh()->GetAnimInstance();

	//攻撃モンタージュが再生中でない場合は、攻撃モンタージュを再生する
	if (!animInst) { return; }

	//リロードの弾薬状態に合わせて射撃または補充処理へ分岐する
	if (m_actionState == EActionState::Reloading) { return; }

	//攻撃モンタージュが再生中でない場合は、攻撃モンタージュを再生する
	if (m_attackHit && m_attackMontage && CanCommitMeleeAttack(UGameplayStatics::GetPlayerPawn(GetWorld(), 0)))
	{
		m_currentAttackIndex++;
		FName nextSection = FName(*FString::Printf(TEXT("Attack%d"), m_currentAttackIndex + 1));

		//攻撃モンタージュの次のセクションが有効な場合は、攻撃モンタージュの次のセクションにジャンプする
		if (m_attackMontage->IsValidSectionName(nextSection))
		{
			m_attackHit = false;
			animInst->Montage_JumpToSection(nextSection, m_attackMontage);
			return;
		}
	}

	//攻撃モンタージュの次のセクションが有効でない場合は、攻撃モンタージュを停止する
	m_attackHit = false;
	m_currentAttackIndex = 0;

	//攻撃モンタージュを停止する
	RestoreDefaultSpeed();

	//攻撃モンタージュを停止する
	SetActionState(EActionState::Idle);
}

//通知区間外のタイマー発射と、構え上げ途中の発射を防ぐ関数
bool AEnemyChara::IsInGunFireWindow() const
{
	if (!IsAttacking() || m_currentStyle != EEnemyAttackStyle::Gun || !IsWeaponReady()) { return false; }
	if (!m_attackMontage) { return true; }
	const UAnimInstance *animation = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!animation || !animation->Montage_IsPlaying(m_attackMontage)) { return false; }
	const float position = animation->Montage_GetPosition(m_attackMontage);
	float fireStart = BIG_NUMBER;
	//銃を取り出した時刻ではなく、腕が照準姿勢に到達した時刻まで発射を待つ
	float aimReady = 0.f;
	float fireEnd = m_attackMontage->GetPlayLength();
	for (const FAnimNotifyEvent &event : m_attackMontage->Notifies)
	{
		if (event.NotifyName == TEXT("Notify_Fire")) { fireStart = FMath::Min(fireStart, event.GetTime()); }
		if (event.NotifyName == TEXT("OnWeaponAimComplete")) { aimReady = FMath::Max(aimReady, event.GetTime()); }
		if (event.NotifyName == TEXT("Notify_Stop")) { fireEnd = FMath::Min(fireEnd, event.GetTime()); }
	}
	return position >= FMath::Max(fireStart, aimReady) && position < fireEnd;
}

//FireSequenceを開始する関数
void AEnemyChara::BeginFireSequence()
{
	//攻撃が有効でない場合は、処理を終了する
	if (!IsValid(this) || IsPendingKillPending() || !GetWorld()) { return; }

	//攻撃が銃でない場合は、処理を終了する
	if (m_actionState == EActionState::Attacking && m_currentStyle == EEnemyAttackStyle::Gun)
	{
		//構え上げ中の通知や別のモンタージュから発射を始めない
		UAnimInstance *animation = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
		if (m_attackMontage && m_attackMontage->IsValidSectionName(TEXT("Fire")) &&
			(!animation || !animation->Montage_IsPlaying(m_attackMontage) ||
			animation->Montage_GetCurrentSection(m_attackMontage) != TEXT("Fire"))) { return; }
		if (IsValid(m_currentGun) && IsInGunFireWindow()) { m_currentGun->StartFire(); }
	}
}

//リロードを完了する関数
void AEnemyChara::FinishReload()
{
	//中断済みの補充通知で別の攻撃状態を解除しない
	if (GetHealthRatio() <= 0.f || !m_isReloading) { return; }
	//リロードタイマーをクリアする
	if (IsValid(m_currentGun)) { m_currentGun->ReloadAmmo(); }
	//リロードフラグを解除する
	m_isReloading = false;
	SetActionState(EActionState::Idle);
}

//近接武器のOverlapからプレイヤーへ一度だけDamageを適用する関数
void AEnemyChara::OnHit(UPrimitiveComponent *_hitComponent, AActor *_otherActor, UPrimitiveComponent *_otherComponent, FVector _normalImpulse,
						const FHitResult &_hit)
{
	//命中したアクターがプレイヤーでない場合は、処理を終了する
	if (_otherActor->ActorHasTag("Player"))
	{
		//命中したアクターがプレイヤーである場合は、プレイヤーのHPを取得する
		APlayerChara *pPlayer = Cast<APlayerChara>(_otherActor);

		//プレイヤーが有効な場合だけ攻撃可能距離に合う攻撃を実行する
		if (!pPlayer) { return; }
		//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
		if (pPlayer->m_damaged) { return; }
	}
}

//敵破棄時に武器、タイマー、一時エフェクトを安全に終了する関数
void AEnemyChara::Destroyed()
{
	//近接攻撃 命中 受付時間を終了する
	CloseMeleeHitWindow();

	//ワールドが有効である場合は、タイマーをクリアする
	if (UWorld *world = GetWorld())
	{
		FTimerManager &timerManager = world->GetTimerManager();
		timerManager.ClearTimer(m_reloadTimerHandle);
		timerManager.ClearTimer(m_laserAttackTimerHandle);
		timerManager.ClearTimer(m_laserStateResetTimerHandle);
		timerManager.ClearTimer(m_laserEffectCleanupTimerHandle);
		timerManager.ClearTimer(m_transientBossEffectCleanupTimerHandle);
		timerManager.ClearTimer(m_teleportTimerHandle);
		timerManager.ClearTimer(m_teleportFinishTimer);
		timerManager.ClearTimer(m_summonTimer);
		timerManager.ClearTimer(m_attackCoolDown);
		timerManager.ClearTimer(m_smashTimer);
	}

	//レーザー攻撃のチャージを停止し、レーザー攻撃のエフェクトを破棄する
	StopLaserCharge();
	CleanupLaserEffect();
	DestroyAllTransientBossEffects();

	//敵銃と近接武器を破棄する
	if (IsValid(m_currentGun)) { m_currentGun->Destroy(); }
	if (IsValid(m_meleeWeapon)) { m_meleeWeapon->Destroy(); }

	//アクター破棄時にタイマーと一時エフェクトを解除する関数
	Super::Destroyed();
}

//全体HPを復元する関数
void AEnemyChara::RestoreFullHealth()
{
	//HPのHP状態に合わせて生存または死亡処理へ分岐する
	if (m_healthComponent) { m_healthComponent->RestoreFullHealth(); }
	//敵ランクがラストボスまたはミドルボスの場合は、ダメージ通知をブロードキャストする
	if (m_enemyRank == EEnemyRank::LastBoss || m_enemyRank == EEnemyRank::MiddleBoss) { m_onDamaged.Broadcast(); }
}

//敵が装備する銃の弾薬を補充して射撃可能な状態へ戻す関数
void AEnemyChara::ReloadWeapon()
{
	PeformReload();
}
