#include "Enemy/Components/EnemyCoverComponent.h"

#include "NavigationSystem.h"
#include "Movement/CharacterPace.h"
#include "NavigationPath.h"
#include "Enemy/EnemyChara.h"
#include "Weapons/EnemyGun.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "Enemy/EnemyTeamTactics.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Enemy/Components/EnemyCombatMemoryComponent.h"
#include "AI/Controllers/EnemyAIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Spawning/SpawnEnemy.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"

//敵の遮蔽物利用に関する定数、列挙型、構造体、関数を定義する
namespace
{
//遮蔽物の有無を調べる射線の地面からの高さ
constexpr float CoverTraceHeight = 60.f;
//プレイヤーに近すぎる遮蔽候補を除外する最低距離
constexpr float MinimumThreatDistance = 450.f;
//遮蔽物の衝突地点から候補位置まで許容する最大距離
constexpr float MaximumCoverToCandidateDistance = 650.f;
}

//遠距離敵の遮蔽物検索と射撃位置評価を初期化する関数
UEnemyCoverComponent::UEnemyCoverComponent()
	: m_searchRadius(2200.f), m_dodgeDistance(350.f), m_repositionCooldown(2.25f), m_lastRepositionTime(-BIG_NUMBER),
	  m_coverSamples(36), m_coverDestination(FVector::ZeroVector), m_lastCoverDirectionFromThreat(FVector::ZeroVector),
	  m_coverArrivalTime(-BIG_NUMBER), m_coverHoldDuration(3.f), m_coverAcceptanceRadius(45.f), m_usingCover(false), m_coverMoveRequested(false)
{
	//通常の遮蔽検索は要求時だけ行い、移動射撃の間だけTickを有効にする
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UEnemyCoverComponent::TickComponent(float _deltaTime, ELevelTick _tickType, FActorComponentTickFunction *_tickFunction)
{
	Super::TickComponent(_deltaTime, _tickType, _tickFunction);
	//退避走行は移動射撃とは独立し、死亡や吹き飛びでも必ず解除する
	if (m_coverRun)
	{
		AEnemyChara *runner = Cast<AEnemyChara>(GetOwner());
		const bool running = runner && m_usingCover && !runner->IsKnockedBack() && runner->GetHealthRatio() > 0.f &&
			!ASpawnEnemy::IsPhaseDisplaying() && GetDistanceToCover() > m_coverAcceptanceRadius;
		UpdateCoverRun(running);
		return;
	}
	AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner());
	AEnemyAIController *controller = enemy ? Cast<AEnemyAIController>(enemy->GetController()) : nullptr;
	AActor *target = m_mobileTarget.Get();
	//射撃中断や見失い後まで横移動を続けず、次の判断へ制御を返す
	if (!m_mobileFire || !enemy || !controller || !target || !enemy->IsAttacking() || enemy->IsReloading() ||
		enemy->m_currentStyle != EEnemyAttackStyle::Gun || enemy->IsKnockedBack() || enemy->GetHealthRatio() <= 0.f ||
		enemy->IsLaserSequenceActive() || ASpawnEnemy::IsPhaseDisplaying() || !controller->CanObserveTarget(target) ||
		GetWorld()->GetTimeSeconds() >= m_mobileUntil || controller->GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		StopMobileFire();
		return;
	}
	controller->SetFocus(target);
	enemy->FaceTarget(target);
}

bool UEnemyCoverComponent::TryMobileFire(AActor *_target)
{
	AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner());
	AEnemyAIController *controller = enemy ? Cast<AEnemyAIController>(enemy->GetController()) : nullptr;
	UNavigationSystemV1 *nav = UNavigationSystemV1::GetCurrent(GetWorld());
	if (m_mobileFire || m_usingCover || !enemy || !controller || !nav || !IsValid(_target) ||
		!enemy->IsInGunFireWindow() || !controller->CanObserveTarget(_target) || !FEnemyTeamTactics::CanRelocate(enemy)) { return false; }
	const FVector origin = enemy->GetActorLocation();
	const FVector away = (origin - _target->GetActorLocation()).GetSafeNormal2D();
	const FVector side = FVector::CrossProduct(FVector::UpVector, away);
	const float distance = FVector::Dist2D(origin, _target->GetActorLocation());
	FCollisionQueryParams query;
	query.AddIgnoredActor(enemy);
	query.AddIgnoredActor(_target);
	const float firstSide = (enemy->GetUniqueID() % 2) ? 1.f : -1.f;
	for (float sign : {firstSide, -firstSide})
	{
		FNavLocation candidate;
		const FVector desired = origin + side * sign * 260.f + away * (distance < 700.f ? 180.f : 0.f);
		if (!nav->ProjectPointToNavigation(desired, candidate, FVector(60.f, 60.f, 180.f))) { continue; }
		if (FEnemyTeamTactics::IsOccupied(enemy, candidate.Location, 280.f)) { continue; }
		UNavigationPath *path = nav->FindPathToLocationSynchronously(GetWorld(), origin, candidate.Location, enemy);
		if (!path || !path->IsValid() || path->IsPartial() || path->GetPathLength() > 450.f || path->GetPathLength() < 120.f) { continue; }
		FHitResult hit;
		const FVector eye = candidate.Location + FVector(0.f, 0.f, enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 1.7f);
		if (GetWorld()->LineTraceSingleByChannel(hit, eye, _target->GetActorLocation(), ECC_Visibility, query)) { continue; }
		UCharacterMovementComponent *movement = enemy->GetCharacterMovement();
		m_savedSpeed = movement->MaxWalkSpeed;
		m_mobileSpeed = FMath::Min(m_savedSpeed, 180.f * CharacterPace::WalkScale);
		m_savedOrient = movement->bOrientRotationToMovement;
		m_savedDesiredRotation = movement->bUseControllerDesiredRotation;
		movement->MaxWalkSpeed = m_mobileSpeed;
		movement->bOrientRotationToMovement = false;
		movement->bUseControllerDesiredRotation = true;
		m_mobileFire = true;
		m_mobileTarget = _target;
		m_mobileUntil = GetWorld()->GetTimeSeconds() + 2.4f;
		controller->SetFocus(_target);
		if (controller->MoveToLocation(candidate.Location, 25.f, false, true, true, true) != EPathFollowingRequestResult::RequestSuccessful)
		{
			StopMobileFire();
			continue;
		}
		CommitReposition();
		SetComponentTickEnabled(true);
		return true;
	}
	return false;
}

void UEnemyCoverComponent::StopMobileFire()
{
	if (m_mobileFire)
	{
		if (AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner()))
		{
			UCharacterMovementComponent *movement = enemy->GetCharacterMovement();
			//ノックバックや別の攻撃で変えた速度は上書きしない
			if (FMath::IsNearlyEqual(movement->MaxWalkSpeed, m_mobileSpeed)) { movement->MaxWalkSpeed = m_savedSpeed; }
			movement->bOrientRotationToMovement = m_savedOrient;
			movement->bUseControllerDesiredRotation = m_savedDesiredRotation;
			if (AAIController *controller = Cast<AAIController>(enemy->GetController())) { controller->StopMovement(); }
		}
		CommitReposition();
	}
	m_mobileFire = false;
	m_mobileTarget.Reset();
	//射撃だけを止めた場合も、退避中の速度と終了判定は更新し続ける
	SetComponentTickEnabled(m_coverRun);
}

//射線、NavMesh、敵との距離を評価して最適な遮蔽物位置を返す関数
bool UEnemyCoverComponent::FindBestCover(AActor *_threat, FVector &_outLocation, bool _forReload)
{
	//遮蔽候補を検索する敵自身
	AActor *owner = GetOwner();
	//移動可能な遮蔽候補を取得するナビゲーションシステム
	UNavigationSystemV1 *navigation = UNavigationSystemV1::GetCurrent(owner);
	//必要なActor、NavMesh、再配置Cooldownが揃わない場合は検索を中止する
	if (!owner || !_threat || !navigation || !CanReposition()) { return false; }

	//有効な遮蔽候補を一つ以上発見したか示す状態
	bool foundCover = false;
	//比較中に最も高い遮蔽候補の評価点
	float bestScore = -BIG_NUMBER;
	//施設に実在する遮蔽物の四辺から、体が壁に重ならない退避候補を作る
	TArray<FVector> samples;
	const AEnemyChara *enemy = Cast<AEnemyChara>(owner);
	const float clearance = enemy ? enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() + 65.f : 110.f;
	for (TActorIterator<AActor> actor(GetWorld()); actor; ++actor)
	{
		TInlineComponentArray<UInstancedStaticMeshComponent *> meshes(*actor);
		for (UInstancedStaticMeshComponent *mesh : meshes)
		{
			if (mesh->GetFName() != TEXT("CoverPanels") || !mesh->GetStaticMesh()) { continue; }
			for (int32 index = 0; index < mesh->GetInstanceCount(); ++index)
			{
				FTransform transform;
				mesh->GetInstanceTransform(index, transform, true);
				const FBox bounds = mesh->GetStaticMesh()->GetBoundingBox().TransformBy(transform);
				if (FVector::DistSquared2D(owner->GetActorLocation(), bounds.GetCenter()) > FMath::Square(m_searchRadius)) { continue; }
				const FVector center = bounds.GetCenter();
				const FVector extent = bounds.GetExtent();
				for (float side : {-1.f, 1.f})
				{
					samples.Add(FVector(center.X + side * (extent.X + clearance), center.Y, bounds.Min.Z + 10.f));
					samples.Add(FVector(center.X, center.Y + side * (extent.Y + clearance), bounds.Min.Z + 10.f));
				}
			}
		}
	}
	//遮蔽物候補のサンプル数に応じて、円周上に等間隔でサンプル点を生成し、評価する
	for (int32 sampleIndex = 0; sampleIndex < m_coverSamples; ++sampleIndex)
	{
		//敵の周囲へ候補を等間隔で並べる角度
		const float angleRadians = 2.f * PI * static_cast<float>(sampleIndex) / static_cast<float>(m_coverSamples);
		//候補を三種類の半径へ分散する番号
		const int32 radiusBand = sampleIndex % 3;
		//検索範囲内へ三重の円を作る現在候補の半径
		const float sampleRadius = m_searchRadius * (0.4f + 0.3f * radiusBand);
		//敵の位置から候補までの平面Offset
		const FVector sampleOffset(FMath::Cos(angleRadians) * sampleRadius, FMath::Sin(angleRadians) * sampleRadius, 0.f);
		samples.Add(owner->GetActorLocation() + sampleOffset);
	}
	for (const FVector &sample : samples)
	{

		//NavMesh上に候補点を投影し、経路が有効かどうかを確認する
		FNavLocation candidate;
		//NavMesh外の候補は敵が移動できないため除外する
		if (!navigation->ProjectPointToNavigation(sample, candidate, FVector(80.f, 80.f, 350.f))) { continue; }
		//経路が有効かどうかを確認し、遮蔽物が敵から見えないかどうかを確認する
		//敵の現在位置から候補まで到達できるか調べる経路
		UNavigationPath *path =
			UNavigationSystemV1::FindPathToLocationSynchronously(owner->GetWorld(), owner->GetActorLocation(), candidate.Location, owner);
		//無効または途中で途切れる経路の候補を除外する
		if (!path || !path->IsValid() || path->IsPartial() || path->GetPathLength() > m_searchRadius * 1.5f) { continue; }
		//遮蔽物への退避でも、相手の足元を横切る近道を選ばない
		if (!FEnemyTeamTactics::IsSafeCombatPath(path->PathPoints, GetKnownPosition(_threat))) { continue; }
		//プレイヤーから直接見える候補は遮蔽物として利用できないため除外する
		if (!IsOccludedFromThreat(_threat, candidate.Location)) { continue; }
		//敵が候補まで移動する距離
		const float travelDistance = FVector::Distance(owner->GetActorLocation(), candidate.Location);
		//候補とプレイヤーの間に確保できる距離
		const float threatDistance = FVector::Distance(GetKnownPosition(_threat), candidate.Location);
		//近すぎる位置と、攻撃用なのに銃弾が届かない位置を除外する
		if (!CanUseCoverAtDistance(threatDistance, _forReload)) { continue; }
		//射撃へ戻れる出口のない遮蔽物を通常の戦闘拠点にしない
		FVector peek;
		if (!_forReload && enemy && enemy->m_currentStyle == EEnemyAttackStyle::Gun && !FindPeekPosition(_threat, candidate.Location, peek)) { continue; }
		//近接型は隠れるだけで後退せず、相手へ近づける足場を選ぶ
		const AEnemyChara *meleeOwner = Cast<AEnemyChara>(owner);
		if (meleeOwner && meleeOwner->m_currentStyle == EEnemyAttackStyle::Melee &&
			threatDistance > FVector::Dist2D(owner->GetActorLocation(), GetKnownPosition(_threat)) - 150.f) { continue; }
		if (FEnemyTeamTactics::IsOccupied(meleeOwner, candidate.Location, 280.f)) { continue; }
		//プレイヤーを中心に見た候補の方向
		const FVector candidateDirection = (candidate.Location - GetKnownPosition(_threat)).GetSafeNormal2D();
		//候補と他の射撃型敵の間に確保できる最小角度
		float minimumPeerAngle = 180.f;
		//角度を比較できる他の射撃型敵が存在するか示す状態
		bool hasRangedPeer = false;
		//別の敵が使用中または移動予約済みの場所へ重ならないための判定
		bool occupied = false;
		//他の射撃型敵と同じ方向へ集まらない候補を探す
		for (TActorIterator<AEnemyChara> iterator(owner->GetWorld()); iterator; ++iterator)
		{
			//陣形の角度比較へ使用する他の敵
			AEnemyChara *peer = *iterator;
			//無効、自分自身、死亡済み、近接型の敵を陣形評価から除外する
			if (!IsValid(peer) || peer == owner || peer->GetHealthRatio() <= 0.f || peer->m_currentStyle != EEnemyAttackStyle::Gun) { continue; }
			//待機中の別室の敵に合わせて、不必要な迂回先を選ばない
			if (peer->IsHidden() || !FEnemyTeamTactics::SharesRoom(Cast<AEnemyChara>(owner), peer)) { continue; }
			const FVector peerLocation = peer->m_coverComponent && peer->m_coverComponent->IsUsingCover()
				? peer->m_coverComponent->GetCoverDestination() : peer->GetActorLocation();
			if (FVector::DistSquared2D(candidate.Location, peerLocation) < FMath::Square(250.f))
			{
				occupied = true;
				break;
			}

			//プレイヤーを中心に見た他の射撃型敵の方向
			const FVector peerDirection = (peerLocation - GetKnownPosition(_threat)).GetSafeNormal2D();
			//位置が重なり方向を計算できない敵を除外する
			if (peerDirection.IsNearlyZero()) { continue; }
			hasRangedPeer = true;

			//遮蔽物候補と他の敵との角度を計算し、最小角度を更新する
			minimumPeerAngle =
				FMath::Min(minimumPeerAngle,
						   FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(candidateDirection, peerDirection), -1.f, 1.f))));
		}
		//他の射撃型敵と異なる方向へ配置するための評価点
		if (occupied) { continue; }
		const float peerAngleScore = hasRangedPeer ? minimumPeerAngle * 1.6f : 90.f;
		//直前の遮蔽位置と異なる方向へ再配置するための評価点
		float previousAngleScore = 60.f;
		//直前の遮蔽方向が記録済みの場合は候補との角度を評価する
		if (!m_lastCoverDirectionFromThreat.IsNearlyZero())
		{
			previousAngleScore = FMath::RadiansToDegrees(
				FMath::Acos(FMath::Clamp(FVector::DotProduct(candidateDirection, m_lastCoverDirectionFromThreat), -1.f, 1.f)));
		}

		//武器の射程を候補評価へ反映する敵自身
		const AEnemyChara *enemyOwner = Cast<AEnemyChara>(owner);
		//敵が射撃しやすいプレイヤーとの距離
		float desiredRange = enemyOwner ? FMath::Max(enemyOwner->GetAttackRange() * 0.85f, 650.f) : 850.f;

		//敵が武器を持っている場合、武器の射程距離に基づいて望ましい距離を調整する
		if (enemyOwner && enemyOwner->m_currentStyle == EEnemyAttackStyle::Gun && enemyOwner->GetCurrentGun())
		{
			desiredRange = FMath::Min(1400.f, enemyOwner->GetCurrentGun()->GetFireRange() * 0.65f);
		}

		//武器の適正距離から外れる候補へ加える減点
		const float rangePenalty = FMath::Abs(threatDistance - desiredRange) * 0.45f;
		//陣形、直前位置、移動距離、武器射程を合成した候補評価点
		const float score = peerAngleScore * 3.f + previousAngleScore * 0.7f - path->GetPathLength() * 0.35f - rangePenalty * 0.2f;
		//現在の最高点を上回る候補を移動先として更新する
		if (score > bestScore)
		{
			bestScore = score;
			_outLocation = candidate.Location;
			foundCover = true;
		}
	}

	//最適な遮蔽物位置が見つかった場合に次回の分散配置へ使用する方向を記録する
	if (foundCover)
	{
		m_lastCoverDirectionFromThreat = (_outLocation - GetKnownPosition(_threat)).GetSafeNormal2D();
	}
	//遮蔽物がない場所で毎回大量の経路検索を繰り返すことを防ぐ
	else { CommitReposition(); }
	//最適な遮蔽物位置が見つかったかどうかを返す
	return foundCover;
}

//反撃用の遮蔽物には射程の余裕を確保し、補充中は安全な退避距離を優先する関数
bool UEnemyCoverComponent::CanUseCoverAtDistance(float _distance, bool _forReload) const
{
	if (!FMath::IsFinite(_distance) || _distance < MinimumThreatDistance) { return false; }
	if (_forReload) { return true; }
	//遮蔽物から身を出しても射程外にならないよう一割の余裕を設ける
	const AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner());
	//近接型は中距離の遮蔽物を接近の中継点にする
	if (enemy && enemy->m_currentStyle == EEnemyAttackStyle::Melee) { return _distance <= 1800.f; }
	const AEnemyGun *gun = enemy ? enemy->GetCurrentGun() : nullptr;
	return gun && _distance <= gun->GetFireRange() * 0.9f;
}

//回避 位置を戦況と有効範囲から選択する関数
bool UEnemyCoverComponent::FindDodgeLocation(AActor *_threat, FVector &_outLocation, bool _ignoreCooldown)
{
	//回避移動を行う敵自身
	AActor *owner = GetOwner();
	//横方向の回避候補をNavMeshへ投影するナビゲーションシステム
	UNavigationSystemV1 *navigation = UNavigationSystemV1::GetCurrent(owner);
	//必要なActor、NavMesh、Cooldownが揃わない場合は回避位置を返さない
	if (!owner || !_threat || !navigation || (!_ignoreCooldown && !CanReposition())) { return false; }

	//敵からプレイヤーへ向かう平面方向
	const FVector threatDirection = (GetKnownPosition(_threat) - owner->GetActorLocation()).GetSafeNormal2D();
	//プレイヤーへの方向と直交する横回避方向
	const FVector rightDirection = FVector::CrossProduct(FVector::UpVector, threatDirection);
	//左右の回避を固定化しないために選ぶ方向の符号
	const float dodgeSide = FMath::RandBool() ? 1.f : -1.f;

	//NavMesh上に候補点を投影し、経路が有効かどうかを確認する
	FNavLocation dodgeLocation;
	//横方向の候補をNavMeshへ投影できない場合は回避を中止する
	if (!navigation->ProjectPointToNavigation(owner->GetActorLocation() + rightDirection * dodgeSide * m_dodgeDistance, dodgeLocation)) { return false; }

	//経路が有効かどうかを確認し、回避位置を更新する
	UNavigationPath *path = UNavigationSystemV1::FindPathToLocationSynchronously(owner->GetWorld(), owner->GetActorLocation(), dodgeLocation.Location, owner);
	if (!path || !path->IsValid() || path->IsPartial()) { return false; }
	//リロード時の横回避にも同じ安全基準を適用し、相手の目の前を横切る経路を選ばない
	if (!FEnemyTeamTactics::IsSafeCombatPath(path->PathPoints, GetKnownPosition(_threat))) { return false; }
	if (FEnemyTeamTactics::IsOccupied(Cast<AEnemyChara>(owner), dodgeLocation.Location, 280.f)) { return false; }
	if (FVector::DistSquared2D(owner->GetActorLocation(), dodgeLocation.Location) < FMath::Square(100.f)) { return false; }
	_outLocation = dodgeLocation.Location;
	return true;
}

//Repositionが現在成立しているかを判定する関数
bool UEnemyCoverComponent::CanReposition() const
{
	if (HasFiringWindow()) { return false; }
	//前回の再配置からCooldownが経過した場合だけ新しい移動を許可する
	return GetWorld() && GetWorld()->GetTimeSeconds() - m_lastRepositionTime >= m_repositionCooldown;
}

//Repositionを対象へ反映し、反映後の状態を確定する関数
void UEnemyCoverComponent::CommitReposition()
{
	//連続した再配置を防ぐために実行時刻を記録する
	m_lastRepositionTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

//遮蔽物を対象へ反映し、反映後の状態を確定する関数
void UEnemyCoverComponent::CommitCover(const FVector &_coverLocation)
{
	StopMobileFire();
	//選択した位置を未到着の遮蔽移動要求として設定する
	m_coverDestination = _coverLocation;
	m_homeCover = _coverLocation;
	m_peekPosition = _coverLocation;
	m_peekCount = 0;
	m_bestDistance = GetOwner() ? FVector::Dist2D(GetOwner()->GetActorLocation(), m_coverDestination) : BIG_NUMBER;
	m_lastProgressTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	m_peeking = false;
	m_fireUntil = -1.f;
	m_coverArrivalTime = -BIG_NUMBER;
	m_usingCover = true;
	m_coverMoveRequested = false;
	CommitReposition();
}

//AndIsAt遮蔽物を最新の入力と状態へ同期する関数
bool UEnemyCoverComponent::UpdateAndIsAtCover()
{
	//遮蔽物を未使用または敵が無効な場合は到着していないと判断する
	if (!m_usingCover || !GetOwner()) { return false; }
	//敵と遮蔽位置の距離が許容範囲内か示す状態
	//顔を出す移動は、壁の内側で到着扱いにならないよう許容距離を狭める
	const float acceptance = m_peeking ? 45.f : m_coverAcceptanceRadius;
	const bool isAtCover = FVector::DistSquared2D(GetOwner()->GetActorLocation(), m_coverDestination) <= FMath::Square(acceptance);
	const float remaining = FVector::Dist2D(GetOwner()->GetActorLocation(), m_coverDestination);
	if (remaining + 20.f < m_bestDistance)
	{
		m_bestDistance = remaining;
		m_lastProgressTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	}
	//通路の閉鎖や味方との接触で到達できない遮蔽物を追い続けない
	if (!isAtCover && GetWorld() && (GetWorld()->GetTimeSeconds() - m_lastProgressTime > 8.f ||
		GetWorld()->GetTimeSeconds() - m_lastRepositionTime > 25.f))
	{
		FinishCoverUse();
		return false;
	}

	//遮蔽物に到達した場合に待機開始時刻を記録する
	if (isAtCover && m_coverArrivalTime < 0.f)
	{
		m_coverArrivalTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		if (m_peeking) { m_fireUntil = m_coverArrivalTime + 4.f + (GetOwner()->GetUniqueID() % 3); }
	}
	return isAtCover;
}

//DistanceTo遮蔽物の現在値を参照側へ渡す関数
float UEnemyCoverComponent::GetDistanceToCover() const
{
	//遮蔽物を使用中の場合だけ敵から遮蔽位置までの平面距離を返す
	return m_usingCover && GetOwner() ? FVector::Dist2D(GetOwner()->GetActorLocation(), m_coverDestination) : 0.f;
}

//遮蔽物が現在成立しているかを判定する関数
bool UEnemyCoverComponent::ShouldHoldCover() const
{
	if (m_peeking) { return false; }
	const AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner());
	//ボスは短く隠れて撃ち返し、通常敵は従来の三秒待機を維持する
	const float holdTime = enemy && enemy->m_currentStyle == EEnemyAttackStyle::Melee ? 0.45f
		: enemy && enemy->m_enemyRank != EEnemyRank::Minion ? 0.65f + (GetOwner()->GetUniqueID() % 3) * 0.1f
		: 0.7f + (GetOwner()->GetUniqueID() % 4) * 0.25f;
	//遮蔽到着から待機時間が経過するまでは同じ場所を維持する
	return m_usingCover && m_coverArrivalTime >= 0.f && GetWorld() && GetWorld()->GetTimeSeconds() - m_coverArrivalTime < holdTime;
}

//予約済みの遮蔽物移動先をAIへ渡し、同じ要求が再利用されないよう消費済みにする関数
bool UEnemyCoverComponent::ConsumeCoverMoveRequest()
{
	//遮蔽物が未選択または要求済みの場合は同じ移動先を再通知しない
	if (!m_usingCover || m_coverMoveRequested) { return false; }
	//現在の遮蔽位置をAIへ一度渡したことを記録する
	m_coverMoveRequested = true;
	return true;
}

//遮蔽物移動要求を初期状態へ戻す関数
void UEnemyCoverComponent::ResetCoverMoveRequest()
{
	//遮蔽物を使用中の場合だけ移動要求を再通知できる状態へ戻す
	if (m_usingCover) { m_coverMoveRequested = false; }
}

//遮蔽物 Useを終了し、専用タイマーと一時フラグを解除する関数
void UEnemyCoverComponent::FinishCoverUse()
{
	UpdateCoverRun(false);
	StopMobileFire();
	//射撃位置へ到着した時だけ再退避までの猶予を作る
	m_fireUntil = -1.f;
	m_peeking = false;
	//次の遮蔽検索へ進むために現在の使用状態と到着時刻を解除する
	CommitReposition();
	m_usingCover = false;
	m_coverMoveRequested = false;
	m_coverArrivalTime = -BIG_NUMBER;
}

//遮蔽物から出た直後に再び隠れてしまうことを防ぐ関数
bool UEnemyCoverComponent::HasFiringWindow() const
{
	return m_usingCover && m_peeking && m_coverArrivalTime >= 0.f && GetWorld() && GetWorld()->GetTimeSeconds() < m_fireUntil;
}

//左右の候補から射線が通り、壁を通過せず到達できる位置を選ぶ関数
bool UEnemyCoverComponent::BeginPeek(AActor *_target)
{
	FVector position;
	if (!m_usingCover || m_peeking || !FindPeekPosition(_target, m_homeCover, position)) { return false; }
	m_peekPosition = position;
	m_coverDestination = position;
	m_bestDistance = FVector::Dist2D(GetOwner()->GetActorLocation(), m_coverDestination);
	m_lastProgressTime = GetWorld()->GetTimeSeconds();
	m_peeking = true;
	m_coverArrivalTime = -BIG_NUMBER;
	m_coverMoveRequested = false;
	CommitReposition();
	return true;
}

//退避位置から短い経路で射線を確保できる出口を探す関数
bool UEnemyCoverComponent::FindPeekPosition(AActor *_target, const FVector &_origin, FVector &_position) const
{
	AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner());
	UNavigationSystemV1 *navigation = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!enemy || !IsValid(_target) || !navigation) { return false; }
	const FVector origin = _origin;
	const FVector right = FVector::CrossProduct(FVector::UpVector, (GetKnownPosition(_target) - origin).GetSafeNormal2D());
	FCollisionQueryParams query;
	query.AddIgnoredActor(enemy);
	query.AddIgnoredActor(_target);
	//近い位置から左右を試し、遮蔽物を大回りせずに顔を出せる場所を優先する
	for (float offset : {180.f, -180.f, 320.f, -320.f, 500.f, -500.f})
	{
		FNavLocation candidate;
		if (!navigation->ProjectPointToNavigation(origin + right * offset, candidate, FVector(80.f, 80.f, 300.f))) { continue; }
		//隠れている間に相手が離れた場合は、射程外での顔出しを中止して追跡へ戻す
		if (!CanUseCoverAtDistance(FVector::Dist(candidate.Location, GetKnownPosition(_target)))) { continue; }
		if (FEnemyTeamTactics::IsOccupied(enemy, candidate.Location, 250.f)) { continue; }
		if (FVector::DistSquared2D(origin, candidate.Location) < FMath::Square(120.f)) { continue; }
		UNavigationPath *path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), origin, candidate.Location, enemy);
		if (!path || !path->IsValid() || path->IsPartial() || path->GetPathLength() > 1100.f) { continue; }
		FHitResult hit;
		//地表の候補点から敵の目の高さへ補正して、遮蔽物越しの誤った射線を避ける
		FVector eyePosition;
		FRotator eyeRotation;
		enemy->GetActorEyesViewPoint(eyePosition, eyeRotation);
		const float eyeHeight = enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + eyePosition.Z - enemy->GetActorLocation().Z;
		const FVector start = candidate.Location + FVector(0.f, 0.f, eyeHeight);
		if (GetWorld()->LineTraceSingleByChannel(hit, start, GetKnownPosition(_target), ECC_Visibility, query)) { continue; }
		_position = candidate.Location;
		return true;
	}
	return false;
}

//射撃位置と退避位置の両方を味方から予約する関数
bool UEnemyCoverComponent::ReservesPosition(const FVector &_position, float _spacing) const
{
	return m_usingCover && (FVector::DistSquared2D(m_homeCover, _position) < FMath::Square(_spacing) ||
		FVector::DistSquared2D(m_peekPosition, _position) < FMath::Square(_spacing));
}

//顔出しを終え、元の遮蔽物へ戻る移動を予約する関数
void UEnemyCoverComponent::ReturnToCover()
{
	if (!m_usingCover || !m_peeking) { return; }
	++m_peekCount;
	m_lastProgressTime = GetWorld()->GetTimeSeconds();
	m_coverDestination = m_homeCover;
	m_bestDistance = FVector::Dist2D(GetOwner()->GetActorLocation(), m_coverDestination);
	m_peeking = false;
	m_fireUntil = -1.f;
	m_coverArrivalTime = -BIG_NUMBER;
	m_coverMoveRequested = false;
	CommitReposition();
}

//遮蔽物への移動と射撃を切り替え、接近されたら別の射撃位置へ離脱する関数
void UEnemyCoverComponent::UpdateRangedCombat(AActor *_target, bool _visible)
{
	AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner());
	AAIController *controller = enemy ? Cast<AAIController>(enemy->GetController()) : nullptr;
	if (!controller || !IsValid(_target) || enemy->m_currentStyle != EEnemyAttackStyle::Gun || enemy->m_isSwitchingWeapon) { return; }
	//銃を装備したまま選んだレーザーの溜めを退避処理で中断しない
	if (enemy->IsLaserSequenceActive()) { return; }
	if (m_mobileFire) { return; }
	//視覚と味方の報告が両方途切れたら、予約だけが残り続けないよう解除する
	const AEnemyAIController *awareness = Cast<AEnemyAIController>(controller);
	if (!_visible && awareness && !awareness->HasCombatAwareness())
	{
		enemy->StopFiring();
		if (m_usingCover) { FinishCoverUse(); }
		return;
	}
	const float distance = FVector::Dist2D(enemy->GetActorLocation(), GetKnownPosition(_target));
	//近くへ回り込まれた時は、遠い退避地点の探索より構えと反撃を優先する
	const AEnemyGun *closeGun = enemy->GetCurrentGun();
	if (_visible && distance < 650.f && closeGun && !closeGun->IsOutOfAmmo() && !enemy->IsReloading())
	{
		if (m_usingCover) { FinishCoverUse(); }
		//射撃区間へ入った後だけ後退射撃を許可し、構える前の横切りを防ぐ
		if (CanReposition() && TryMobileFire(_target)) { return; }
		controller->StopMovement();
		controller->SetFocus(_target);
		enemy->FaceTarget(_target);
		return;
	}
	if (!m_usingCover)
	{
		if (controller->GetMoveStatus() == EPathFollowingStatus::Moving) { return; }
		//目視できた相手に対してだけ拠点を選び、壁越しの現在位置を利用しない
		if (!_visible || enemy->IsReloading() || !CanReposition() || !FEnemyTeamTactics::CanRelocate(enemy)) { return; }
		FVector position;
		if (!FindBestCover(_target, position))
		{
			//遮蔽物が見つからなくても射撃可能なら留まり、探索のたびに目の前を横切らない
			const AEnemyGun *gun = enemy->GetCurrentGun();
			const bool underPressure = enemy->m_combatMemoryComponent && enemy->m_combatMemoryComponent->IsUnderPressure();
			if (_visible && gun && !underPressure && FVector::Dist2D(enemy->GetActorLocation(), GetKnownPosition(_target)) <= gun->GetFireRange())
			{
				TryMobileFire(_target);
				return;
			}
			//近くに遮蔽物がなく射程外なら、相手本人ではなく射撃可能な側面へ展開する
			if (FEnemyTeamTactics::FindFirePosition(enemy, _target, position))
			{
				enemy->StopFiring();
				controller->MoveToLocation(position, 45.f, false);
			}
			return;
		}
		enemy->StopFiring();
		CommitCover(position);
	}
	if (!UpdateAndIsAtCover())
	{
		if (!m_usingCover) { return; }
		//弾切れで安全地点へ退避する間だけ走り、射撃の顔出し移動は通常速度に保つ
		const AEnemyGun *gun = enemy->GetCurrentGun();
		UpdateCoverRun(!m_peeking && gun && (gun->IsOutOfAmmo() || enemy->IsReloading() || m_coverRun));
		enemy->StopFiring();
		enemy->StopCombatIdleAnimation();
		controller->ClearFocus(EAIFocusPriority::Gameplay);
		if (controller->GetMoveStatus() != EPathFollowingStatus::Moving) { ResetCoverMoveRequest(); }
		if (ConsumeCoverMoveRequest() && controller->MoveToLocation(m_coverDestination, 30.f, false) == EPathFollowingRequestResult::Failed)
		{
			FinishCoverUse();
		}
		return;
	}
	UpdateCoverRun(false);
	controller->StopMovement();
	//退避先へ着いてから補充し、空のマガジンのまま顔出しを繰り返さない
	const AEnemyGun *reloadGun = enemy->GetCurrentGun();
	if (!m_peeking && reloadGun && reloadGun->IsOutOfAmmo() && reloadGun->GetTotalAmmo() > 0)
	{
		enemy->PeformReload();
		return;
	}
	if (m_peeking)
	{
		//横移動の向きから最後に目撃した方向へ向き直る時間を確保してから射撃を判断する
		controller->SetFocalPoint(GetKnownPosition(_target));
		if (!_visible && GetWorld()->GetTimeSeconds() - m_coverArrivalTime < 0.6f) { return; }
		//射線を失った時も同じ場所へ撃ち続けず、予約済みの退避位置へ戻る
		const bool pressured = enemy->m_combatMemoryComponent && enemy->m_combatMemoryComponent->IsUnderPressure() &&
			GetWorld()->GetTimeSeconds() - m_coverArrivalTime > 1.f;
		if (!_visible || !HasFiringWindow() || enemy->IsReloading() || pressured)
		{
			enemy->StopFiring();
			ReturnToCover();
		}
		else { controller->SetFocus(_target); }
		return;
	}
	enemy->StopFiring();
	if (enemy->IsReloading() || ShouldHoldCover()) { return; }
	//同じ出口を二回使った後は戦況を再評価し、ボスの武器変更も再び許可する
	const bool ineffective = enemy->m_combatMemoryComponent && enemy->m_combatMemoryComponent->GetShotFailure() > 0.6f;
	if (m_peekCount >= (ineffective ? 1 : 2)) { FinishCoverUse(); return; }
	//遮蔽物を回り込まれた時は現在の拠点を破棄して次の判断へ戻す
	if (!IsOccludedFromThreat(_target, m_homeCover) || !BeginPeek(_target)) { FinishCoverUse(); }
}

//弾切れ退避に走行アニメーションを合わせ、退避終了時に元の設定を復元する関数
void UEnemyCoverComponent::UpdateCoverRun(bool _running)
{
	AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner());
	if (!enemy || !enemy->GetCharacterMovement() || !enemy->GetMesh()) { return; }
	UCharacterMovementComponent *movement = enemy->GetCharacterMovement();
	UAnimInstance *animation = enemy->GetMesh()->GetAnimInstance();
	if (!_running)
	{
		if (!m_coverRun) { return; }
		if (FMath::IsNearlyEqual(movement->MaxWalkSpeed, m_runSpeed)) { movement->MaxWalkSpeed = m_beforeRunSpeed; }
		movement->bOrientRotationToMovement = m_runOrient;
		movement->bUseControllerDesiredRotation = m_runDesiredRotation;
		enemy->bUseControllerRotationYaw = m_runControllerYaw;
		if (animation && m_runMontage) { animation->Montage_Stop(0.15f, m_runMontage); }
		m_runMontage = nullptr;
		m_coverRun = false;
		SetComponentTickEnabled(m_mobileFire);
		return;
	}
	if (!m_coverRun)
	{
		//警戒減速を退避走行の基準速度として保存しない
		if (AEnemyAIController *controller = Cast<AEnemyAIController>(enemy->GetController())) { controller->RestoreAlertPace(); }
		m_beforeRunSpeed = movement->MaxWalkSpeed;
		m_runSpeed = m_beforeRunSpeed * 1.6f;
		movement->MaxWalkSpeed = m_runSpeed;
		//前進走行の脚と移動方向を揃え、リロード中に相手へ横滑りしないようにする
		m_runOrient = movement->bOrientRotationToMovement;
		m_runDesiredRotation = movement->bUseControllerDesiredRotation;
		m_runControllerYaw = enemy->bUseControllerRotationYaw;
		movement->bOrientRotationToMovement = true;
		movement->bUseControllerDesiredRotation = false;
		enemy->bUseControllerRotationYaw = false;
		m_coverRun = true;
		//既存の骨格対応済み素材を使い、異なる骨格の走行を無理に再生しない
		const TCHAR *path = enemy->m_enemyRank == EEnemyRank::Minion
			? TEXT("/Game/Enemy/Animations/Gun_Animation/Rifle_Run.Rifle_Run")
			: TEXT("/Game/Enemy/Animations/Boss_Animation/Run_With_Sword.Run_With_Sword");
		UAnimSequence *run = LoadObject<UAnimSequence>(nullptr, path);
		if (animation && run && enemy->GetMesh()->GetSkeletalMeshAsset() &&
			run->GetSkeleton() == enemy->GetMesh()->GetSkeletalMeshAsset()->GetSkeleton())
		{
			m_runMontage = animation->PlaySlotAnimationAsDynamicMontage(run, TEXT("RetreatSlot"), 0.15f, 0.15f, 1.f, 1000);
		}
		SetComponentTickEnabled(true);
	}
	//停止中に足だけ走らせず、実移動速度とメッシュ倍率から再生速度を調整する
	if (animation && m_runMontage)
	{
		const float scale = FMath::Max(FMath::Abs(enemy->GetMesh()->GetComponentScale().X), 0.1f);
		//銃兵の素材は一周期0.667秒で腰が161.58cm進むため、その歩幅を基準にする
		const float strideSpeed = enemy->m_enemyRank == EEnemyRank::Minion ? 242.36f : 400.f;
		animation->Montage_SetPlayRate(m_runMontage, FMath::Clamp(enemy->GetVelocity().Size2D() / (strideSpeed * scale), 0.f, 2.f));
	}
}

//遮蔽物が候補位置の近くでプレイヤーの射線を遮っているか判定する関数
bool UEnemyCoverComponent::IsOccludedFromThreat(AActor *_threat, const FVector &_candidate) const
{
	//敵自身とプレイヤーを射線判定から除外するCollision設定
	FCollisionQueryParams queryParams(SCENE_QUERY_STAT(EnemyCoverTrace), false);
	queryParams.AddIgnoredActor(GetOwner());
	queryParams.AddIgnoredActor(_threat);

	//プレイヤーから遮蔽候補までの射線結果
	FHitResult hit;
	//足元の凹凸を避けるため高さを加えた射線開始位置
	const FVector start = GetKnownPosition(_threat) + FVector(0.f, 0.f, CoverTraceHeight);
	//足元の凹凸を避けるため高さを加えた射線終了位置
	const FVector end = _candidate + FVector(0.f, 0.f, CoverTraceHeight);
	//候補の手前で射線を遮る物体が存在するか示す状態
	const bool hasBlockingCover = GetWorld() && GetWorld()->LineTraceSingleByChannel(hit, start, end, ECC_Visibility, queryParams);

	//遮蔽物が敵から見えない場合、遮蔽物候補点が有効かどうかを判定する
	if (!hasBlockingCover || Cast<APawn>(hit.GetActor()) ||
		FVector::DistSquared(hit.ImpactPoint, end) > FMath::Square(MaximumCoverToCandidateDistance)) { return false; }
	//足元だけでなく立った体の上部と左右も遮る場所に限定し、味方を盾として数えない
	const AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner());
	if (!enemy) { return false; }
	const float height = enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 1.8f;
	const float width = enemy->GetCapsuleComponent()->GetScaledCapsuleRadius() * 0.75f;
	const FVector side = FVector::CrossProduct((_candidate - start).GetSafeNormal2D(), FVector::UpVector);
	for (float shoulder : {-1.f, 0.f, 1.f})
	{
		const FVector upper = _candidate + FVector(0.f, 0.f, height) + side * width * shoulder;
		FHitResult bodyHit;
		if (!GetWorld()->LineTraceSingleByChannel(bodyHit, start, upper, ECC_Visibility, queryParams) ||
			Cast<APawn>(bodyHit.GetActor()) || FVector::DistSquared(bodyHit.ImpactPoint, upper) > FMath::Square(MaximumCoverToCandidateDistance))
		{
			return false;
		}
	}
	return true;
}

//壁越しの現在位置ではなく、視覚や味方の報告で記憶した位置を使う関数
FVector UEnemyCoverComponent::GetKnownPosition(AActor *_target) const
{
	if (!IsValid(_target)) { return FVector::ZeroVector; }
	const AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner());
	const AEnemyAIController *controller = enemy ? Cast<AEnemyAIController>(enemy->GetController()) : nullptr;
	if (controller && !controller->HasActiveVisualContact() && controller->HasCombatAwareness())
	{
		return controller->GetLastKnownPlayerLocation();
	}
	return _target->GetActorLocation();
}
