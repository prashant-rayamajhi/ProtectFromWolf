#pragma once

#include "CoreMinimal.h"

namespace EnemyBlackboardKeys
{
inline const FName TargetActor(TEXT("TargetActor"));
inline const FName PatrolLocation(TEXT("PatrolLocation"));
inline const FName CanSeeTarget(TEXT("CanSeeTarget"));
inline const FName IsInAttackRange(TEXT("IsInAttackRange"));
inline const FName ShouldRetreat(TEXT("ShouldRetreat"));
inline const FName IsActionLocked(TEXT("IsActionLocked"));
inline const FName IsPaused(TEXT("IsPaused"));
inline const FName IsDead(TEXT("IsDead"));
inline const FName TargetDistance(TEXT("TargetDistance"));
}
