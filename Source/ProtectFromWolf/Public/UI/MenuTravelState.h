#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MenuTravelState.generated.h"

//タイトルからの開始演出をマップ読み込みの前後で引き継ぐクラス
UCLASS()
class PROTECTFROMWOLF_API UMenuTravelState : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	//本編を読み込んだ後、操作開始前にタイトルを表示する予約
	bool m_showTitle = false;
	//次に開く本編でTPSからFPSへの導入を再生するための予約
	bool m_playArrival = false;
};
