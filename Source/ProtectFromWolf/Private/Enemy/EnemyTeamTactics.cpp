#include "Enemy/EnemyTeamTactics.h"
#include "Enemy/EnemyChara.h"
#include "Enemy/Components/EnemyCoverComponent.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Weapons/EnemyGun.h"
#include "Navigation/PathFollowingComponent.h"

bool FEnemyTeamTactics::SharesRoom(const AEnemyChara *_left, const AEnemyChara *_right)
{
	if (!_left || !_right || _left->GetWorld() != _right->GetWorld()) { return false; }
	//片方だけに所属がある場合も別の戦闘として扱う
	FName leftRoom;
	FName rightRoom;
	for (FName tag : _left->Tags) { if (tag.ToString().StartsWith(TEXT("CombatRoom_"))) { leftRoom = tag; break; } }
	for (FName tag : _right->Tags) { if (tag.ToString().StartsWith(TEXT("CombatRoom_"))) { rightRoom = tag; break; } }
	if (!leftRoom.IsNone() || !rightRoom.IsNone()) { return leftRoom == rightRoom; }
	return FVector::DistSquared2D(_left->GetActorLocation(), _right->GetActorLocation()) < FMath::Square(5000.f);
}

bool FEnemyTeamTactics::IsOccupied(const AEnemyChara *_enemy, const FVector &_position, float _spacing)
{
	if (!_enemy) { return true; }
	for (TActorIterator<AEnemyChara> it(_enemy->GetWorld()); it; ++it)
	{
		//死亡済みや別室の敵には場所を予約させない
		const AEnemyChara *peer = *it;
		if (peer == _enemy || peer->IsHidden() || peer->GetHealthRatio() <= 0.f || !SharesRoom(_enemy, peer)) { continue; }
		if (FVector::DistSquared2D(peer->GetActorLocation(), _position) < FMath::Square(_spacing)) { return true; }
		if (peer->m_coverComponent && peer->m_coverComponent->ReservesPosition(_position, _spacing)) { return true; }
		//移動中の味方の直近の進行先を塞がない
		const AAIController *controller = Cast<AAIController>(peer->GetController());
		if (controller && controller->GetMoveStatus() == EPathFollowingStatus::Moving &&
			FVector::DistSquared2D(controller->GetImmediateMoveDestination(), _position) < FMath::Square(_spacing)) { return true; }
		//経路途中の角だけでなく最終目的地も予約し、同じ射撃地点への合流を防ぐ
		if (controller && controller->GetMoveStatus() == EPathFollowingStatus::Moving && controller->GetPathFollowingComponent())
		{
			const FNavPathSharedPtr path = controller->GetPathFollowingComponent()->GetPath();
			if (path.IsValid() && !path->GetPathPoints().IsEmpty() &&
				FVector::DistSquared2D(path->GetPathPoints().Last().Location, _position) < FMath::Square(_spacing)) { return true; }
		}
	}
	return false;
}

bool FEnemyTeamTactics::CanRelocate(const AEnemyChara *_enemy)
{
	if (!_enemy) { return false; }
	int32 peers = 0;
	int32 relocating = 0;
	for (TActorIterator<AEnemyChara> it(_enemy->GetWorld()); it; ++it)
	{
		const AEnemyChara *peer = *it;
		if (peer == _enemy || peer->IsHidden() || peer->GetHealthRatio() <= 0.f || !SharesRoom(_enemy, peer) ||
			peer->m_currentStyle != EEnemyAttackStyle::Gun) { continue; }
		++peers;
		const AAIController *controller = Cast<AAIController>(peer->GetController());
		//遮蔽物で待機する味方は移動枠を消費せず、実際に経路を進んでいる味方だけ数える
		if (controller && controller->GetMoveStatus() == EPathFollowingStatus::Moving) { ++relocating; }
	}
	//援護射撃中でも移動枠を無制限に増やさず、射撃位置に残る味方を確保する
	return relocating < FMath::Max(1, (peers + 1) / 2);
}

bool FEnemyTeamTactics::CanApproachMelee(const AEnemyChara *_enemy, const AActor *_target)
{
	if (!_enemy || !_target) { return false; }
	//攻撃中の味方を優先し、空いた枠を近い敵へ割り当てる
	const float distance = FVector::DistSquared2D(_enemy->GetActorLocation(), _target->GetActorLocation());
	const int32 priority = _enemy->IsAttacking() ? 0 : (_enemy->m_enemyRank != EEnemyRank::Minion && _enemy->IsTargetWithinMeleeStrikeRange(_target) ? 1 : 2);
	int32 ahead = 0;
	for (TActorIterator<AEnemyChara> it(_enemy->GetWorld()); it; ++it)
	{
		const AEnemyChara *peer = *it;
		if (peer == _enemy || peer->IsHidden() || peer->GetHealthRatio() <= 0.f || !SharesRoom(_enemy, peer) ||
			peer->m_currentStyle != EEnemyAttackStyle::Melee || peer->IsKnockedBack()) { continue; }
		const float peerDistance = FVector::DistSquared2D(peer->GetActorLocation(), _target->GetActorLocation());
		const int32 peerPriority = peer->IsAttacking() ? 0 : (peer->m_enemyRank != EEnemyRank::Minion && peer->IsTargetWithinMeleeStrikeRange(_target) ? 1 : 2);
		const bool nearer = peerDistance < distance || (FMath::IsNearlyEqual(peerDistance, distance) && peer->GetUniqueID() < _enemy->GetUniqueID());
		if (peerPriority < priority || (peerPriority == priority && nearer)) { ++ahead; }
	}
	return ahead < 2;
}

bool FEnemyTeamTactics::FindFirePosition(AEnemyChara *_enemy, AActor *_target, FVector &_position, const FVector *_knownPosition)
{
	if (!_enemy || !IsValid(_target)) { return false; }
	UNavigationSystemV1 *nav = UNavigationSystemV1::GetCurrent(_enemy);
	if (!nav) { return false; }
	//近い候補から左右に展開し、敵の並び順が変わっても陣形を一斉に組み直さない
	const FVector origin = _enemy->GetActorLocation();
	const FVector targetPosition = _knownPosition ? *_knownPosition : _target->GetActorLocation();
	const FVector away = (origin - targetPosition).GetSafeNormal2D();
	const float range = _enemy->GetCurrentGun() ? _enemy->GetCurrentGun()->GetFireRange() * 0.8f : 1400.f;
	const float radius = FMath::Clamp(FVector::Dist2D(origin, targetPosition), 850.f, FMath::Max(range, 900.f));
	FVector eye;
	FRotator rotation;
	_enemy->GetActorEyesViewPoint(eye, rotation);
	const float eyeHeight = _enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + eye.Z - origin.Z;
	FCollisionQueryParams query;
	query.AddIgnoredActor(_enemy);
	query.AddIgnoredActor(_target);
	//一つの距離の六候補が塞がれただけで停止せず、内側の足場も比較する
	bool found = false;
	float bestScore = -BIG_NUMBER;
	for (float distanceScale : {1.f, 0.7f, 0.45f})
	for (float angle : {30.f, -30.f, 60.f, -60.f, 90.f, -90.f, 120.f, -120.f, 180.f})
	{
		FNavLocation candidate;
		const FVector desired = targetPosition + away.RotateAngleAxis(angle, FVector::UpVector) * FMath::Max(650.f, radius * distanceScale);
		if (!nav->ProjectPointToNavigation(desired, candidate, FVector(120.f, 120.f, 350.f))) { continue; }
		if (IsOccupied(_enemy, candidate.Location, 300.f)) { continue; }
		if (FVector::DistSquared2D(origin, candidate.Location) < FMath::Square(150.f)) { continue; }
		FHitResult hit;
		if (_enemy->GetWorld()->LineTraceSingleByChannel(hit, candidate.Location + FVector(0.f, 0.f, eyeHeight),
			targetPosition, ECC_Visibility, query)) { continue; }
		UNavigationPath *path = nav->FindPathToLocationSynchronously(_enemy, origin, candidate.Location, _enemy);
		if (!path || !path->IsValid() || path->IsPartial() || path->GetPathLength() > 5000.f) { continue; }
		//別角度への展開でも相手の足元を横切らず、現在距離より危険な近道を除外する
		if (!IsSafeCombatPath(path->PathPoints, targetPosition)) { continue; }
		//味方と同じ方向を避けつつ、長い迂回より近い射撃可能地点を優先する
		float separation = 90.f;
		const FVector direction = (candidate.Location - targetPosition).GetSafeNormal2D();
		for (TActorIterator<AEnemyChara> peer(_enemy->GetWorld()); peer; ++peer)
		{
			if (*peer == _enemy || peer->IsHidden() || peer->GetHealthRatio() <= 0.f || !SharesRoom(_enemy, *peer)) { continue; }
			const FVector peerDirection = (peer->GetActorLocation() - targetPosition).GetSafeNormal2D();
			separation = FMath::Min(separation, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(direction, peerDirection), -1.f, 1.f))));
		}
		const float score = separation * 8.f - path->GetPathLength() * 0.35f;
		if (score <= bestScore) { continue; }
		bestScore = score;
		_position = candidate.Location;
		found = true;
	}
	return found;
}

bool FEnemyTeamTactics::IsSafeCombatPath(const TArray<FVector> &_points, const FVector &_threat)
{
	if (_points.Num() < 2 || _threat.ContainsNaN()) { return false; }
	for (const FVector &point : _points) { if (point.ContainsNaN()) { return false; } }
	//至近距離では現在距離より内側への横切りだけを除外し、退路を塞がない
	const float clearance = FMath::Min(450.f, FVector::Dist2D(_points[0], _threat) * 0.8f);
	const FVector threat(_threat.X, _threat.Y, 0.f);
	for (int32 index = 1; index < _points.Num(); ++index)
	{
		const FVector start(_points[index - 1].X, _points[index - 1].Y, 0.f);
		const FVector end(_points[index].X, _points[index].Y, 0.f);
		if (FVector::DistSquared(threat, FMath::ClosestPointOnSegment(threat, start, end)) < FMath::Square(clearance)) { return false; }
	}
	return true;
}
