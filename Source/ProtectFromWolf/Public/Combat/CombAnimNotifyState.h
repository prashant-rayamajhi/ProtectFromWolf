

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "CombAnimNotifyState.generated.h"

UCLASS(Blueprintable)
//狼男コンボで次の攻撃入力を受け付ける時間帯を通知するNotifyState
class PROTECTFROMWOLF_API UCombAnimNotifyState : public UAnimNotifyState
{
	GENERATED_BODY()

  public:
	virtual void NotifyBegin(USkeletalMeshComponent *_meshComp, UAnimSequenceBase *_animation, float _totalDuration,
							 const FAnimNotifyEventReference &_eventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent *_meshComp, UAnimSequenceBase *_animation, const FAnimNotifyEventReference &_eventReference) override;
};
