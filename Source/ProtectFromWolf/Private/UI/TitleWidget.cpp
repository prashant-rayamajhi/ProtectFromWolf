#include "UI/TitleWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Player/PlayerChara.h"
#include "Kismet/GameplayStatics.h"

//タイトル文字を設定し、ゲーム開始ボタンへクリックイベントを登録する関数
void UTitleWidget::NativeConstruct()
{
	//親クラスのNativeConstructを呼び出す
	Super::NativeConstruct();
	//Blueprint内の部品名を変えずに、表示に使う参照を取得する
	m_title = Cast<UTextBlock>(GetWidgetFromName(TEXT("m_pTitle")));
	m_inGameButton = Cast<UButton>(GetWidgetFromName(TEXT("m_pInGameButton")));

	//タイトル文字が有効な場合、文字色と表示テキストを設定する
	if (m_title)
	{
		m_title->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 1.f)));

		m_title->SetText(FText::FromString(TEXT("Protect From Wolf")));
	}
}
