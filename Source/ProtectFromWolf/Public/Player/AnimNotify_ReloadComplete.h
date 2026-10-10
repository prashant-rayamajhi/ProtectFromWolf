#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_ReloadComplete.generated.h"

UCLASS(meta = (DisplayName = "Reload Complete"))
//リロードモンタージュ終端でプレイヤーの弾薬補充を確定するAnimNotify
class PROTECTFROMWOLF_API UAnimNotify_ReloadComplete : public UAnimNotify
{
	GENERATED_BODY()

  public:
	virtual void Notify(USkeletalMeshComponent *_meshComp, UAnimSequenceBase *_animation, const FAnimNotifyEventReference &_eventReference) override;

	//Animation Notifyへ表示する名前を取得する関数
	virtual FString GetNotifyName_Implementation() const override;
};
