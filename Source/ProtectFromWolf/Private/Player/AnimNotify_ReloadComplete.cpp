#include "Player/AnimNotify_ReloadComplete.h"

#include "Components/SkeletalMeshComponent.h"
#include "Player/PlayerChara.h"

//リロードモンタージュの通知時点でマガジン補充を確定し、射撃入力を再許可する関数
void UAnimNotify_ReloadComplete::Notify(USkeletalMeshComponent *_meshComp, UAnimSequenceBase *_animation,
										const FAnimNotifyEventReference &_eventReference)
{
	Super::Notify(_meshComp, _animation, _eventReference);
	//Meshが無効な場合は担当機能を実行できないため終了する
	if (!_meshComp) { return; }

	//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
	if (APlayerChara *player = Cast<APlayerChara>(_meshComp->GetOwner())) { player->CompleteReloadFromNotify(); }
}

//通知名前実装を取得する関数
FString UAnimNotify_ReloadComplete::GetNotifyName_Implementation() const
{
	return TEXT("Reload Complete");
}
