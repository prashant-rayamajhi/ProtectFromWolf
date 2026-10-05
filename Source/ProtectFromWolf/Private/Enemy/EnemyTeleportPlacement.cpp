#include "Enemy/EnemyTeleportPlacement.h"
#include "Enemy/EnemyChara.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"

bool FEnemyTeleportPlacement::HasSupport(AEnemyChara *_enemy, const FVector &_center)
{
	if (!IsValid(_enemy) || !_enemy->GetWorld() || _center.ContainsNaN()) { return false; }
	//床の端から体がはみ出さないよう、カプセルの外周も調べる
	const UCapsuleComponent *capsule = _enemy->GetCapsuleComponent();
	const float height = capsule->GetScaledCapsuleHalfHeight();
	const float radius = capsule->GetScaledCapsuleRadius();
	FCollisionQueryParams query(SCENE_QUERY_STAT(EnemyTeleportSupport), false, _enemy);
	for (const FVector offset : {FVector::ZeroVector, FVector(radius, 0.f, 0.f), FVector(-radius, 0.f, 0.f),
		FVector(0.f, radius, 0.f), FVector(0.f, -radius, 0.f)})
	{
		//予定した底面の近くに歩行可能な床がない候補を拒否する
		const FVector bottom = _center + offset - FVector(0.f, 0.f, height);
		FHitResult floor;
		if (!_enemy->GetWorld()->LineTraceSingleByChannel(floor, bottom + FVector(0.f, 0.f, 15.f),
			bottom - FVector(0.f, 0.f, 20.f), ECC_Pawn, query) || !_enemy->GetCharacterMovement()->IsWalkable(floor)) { return false; }
	}
	//空間補正で床の外へ押し出されないよう、移動前にカプセル全体を検査する
	return !_enemy->GetWorld()->OverlapBlockingTestByProfile(_center, FQuat::Identity, capsule->GetCollisionProfileName(),
		FCollisionShape::MakeCapsule(radius, height), query);
}

bool FEnemyTeleportPlacement::Resolve(AEnemyChara *_enemy, const FVector &_desired, FVector &_center)
{
	if (!IsValid(_enemy) || _desired.ContainsNaN()) { return false; }
	UNavigationSystemV1 *nav = UNavigationSystemV1::GetCurrent(_enemy);
	FNavLocation point;
	//ナビゲーション取得失敗時に、未確認の座標をそのまま採用しない
	if (!nav || !nav->ProjectPointToNavigation(_desired, point, FVector(200.f, 200.f, 350.f))) { return false; }
	UNavigationPath *path = nav->FindPathToLocationSynchronously(_enemy, _enemy->GetActorLocation(), point.Location, _enemy);
	if (!path || !path->IsValid() || path->IsPartial() || path->GetPathLength() > 4000.f) { return false; }
	//ナビゲーション面ではなく、実際の床面からカプセル中心の高さを決める
	FCollisionQueryParams query(SCENE_QUERY_STAT(EnemyTeleportFloor), false, _enemy);
	FHitResult floor;
	if (!_enemy->GetWorld()->LineTraceSingleByChannel(floor, point.Location + FVector(0.f, 0.f, 50.f),
		point.Location - FVector(0.f, 0.f, 100.f), ECC_Pawn, query) || !_enemy->GetCharacterMovement()->IsWalkable(floor)) { return false; }
	const FVector center = floor.ImpactPoint + FVector(0.f, 0.f, _enemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f);
	if (!HasSupport(_enemy, center)) { return false; }
	_center = center;
	return true;
}
