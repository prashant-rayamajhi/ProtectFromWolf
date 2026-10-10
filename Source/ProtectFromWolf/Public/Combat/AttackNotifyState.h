

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AttackNotifyState.generated.h"

UCLASS(Blueprintable)
//攻撃モンタージュ中だけ狼男または敵の近接当たり判定を有効化するNotifyState
class PROTECTFROMWOLF_API UAttackNotifyState : public UAnimNotifyState
{
	GENERATED_BODY()

  public:
	virtual void NotifyBegin(USkeletalMeshComponent *_meshComp, UAnimSequenceBase *_animation, float _totalDuration,
							 const FAnimNotifyEventReference &_eventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent *_meshComp, UAnimSequenceBase *_animation, const FAnimNotifyEventReference &_eventReference) override;
};
