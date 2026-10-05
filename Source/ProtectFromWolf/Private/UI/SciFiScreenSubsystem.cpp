#include "UI/SciFiScreenSubsystem.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "TimerManager.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "UI/SciFiScreenWidget.h"
#include "UI/MenuCharacterStage.h"
#include "UI/MenuTravelState.h"
#include "UI/PlayerUI.h"
#include "Audio/SciFiAudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "UnrealClient.h"
#include "EngineUtils.h"
#include "Spawning/CombatArena.h"
#include "Player/PlayerChara.h"
#include "Kismet/GameplayStatics.h"

//Level開始時にマップ種別へ対応する画面を表示する関数
void USciFiScreenSubsystem::OnWorldBeginPlay(UWorld &_world)
{
	Super::OnWorldBeginPlay(_world);
	const FString mapName = _world.GetMapName();
	//タイトルを本編内に置き、開始ボタンの後にはマップを読み直さない
	if (mapName.Contains(TEXT("GameTitle")))
	{
		if (UMenuTravelState *travel = _world.GetGameInstance()->GetSubsystem<UMenuTravelState>()) { travel->m_showTitle = true; }
		_world.GetTimerManager().SetTimerForNextTick(this, &USciFiScreenSubsystem::OpenCombatLevel);
		return;
	}
	ESciFiScreenMode mode;
	bool isCombatLoading = mapName.Contains(TEXT("MainLevel"));
	if (isCombatLoading)
	{
		if (UMenuTravelState *travel = _world.GetGameInstance()->GetSubsystem<UMenuTravelState>())
		{
			m_playArrival = travel->m_playArrival;
			m_titleInCombat = travel->m_showTitle;
			travel->m_showTitle = false;
			travel->m_playArrival = false;
		}
	}
	if (m_titleInCombat) { mode = ESciFiScreenMode::Title; isCombatLoading = false; }
	else if (isCombatLoading) mode = ESciFiScreenMode::Loading;
	else if (mapName.Contains(TEXT("GameTitle")))
		mode = ESciFiScreenMode::Title;
	else if (mapName.Contains(TEXT("GameClear")))
		mode = ESciFiScreenMode::GameClear;
	else if (mapName.Contains(TEXT("GameOver")))
		mode = ESciFiScreenMode::GameOver;
	else
		return;

	if (!isCombatLoading) RemoveLegacyScreenWidgets(_world);

	APlayerController *playerController = _world.GetFirstPlayerController();
	//プレイヤーが無効な場合は画面遷移とGame Modeに合うUIを表示できないため終了する
	if (!playerController) { return; }
	m_screenWidget = CreateWidget<USciFiScreenWidget>(playerController, USciFiScreenWidget::StaticClass());
	//Widgetが無効な場合は画面遷移とGame Modeに合うUIを表示できないため終了する
	if (!m_screenWidget) { return; }
	m_screenWidget->SetScreenMode(mode);
	if (!isCombatLoading)
	{
		//ゲーム用Pawnを流用せず、離れた位置の表示専用セットを撮影する
		FActorSpawnParameters spawn;
		spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		spawn.ObjectFlags |= RF_Transient;
		m_menuStage = _world.SpawnActor<AMenuCharacterStage>(FVector(0.f, 0.f, 50000.f), FRotator::ZeroRotator, spawn);
		const bool hasScene = m_menuStage && (m_titleInCombat ? m_menuStage->StartMapTitle(Cast<APlayerChara>(playerController->GetPawn())) : m_menuStage->SetPresentation(mode));
		m_screenWidget->SetSceneVisible(hasScene);
		if (hasScene)
		{
			playerController->bAutoManageActiveCameraTarget = false;
			playerController->SetViewTarget(m_menuStage);
		}
		//メニューでゲーム用プレイヤーが入力に反応しないようにする
		if (APawn *pawn = playerController->GetPawn())
		{
			if (!m_titleInCombat) { pawn->SetActorHiddenInGame(true); }
			else
			{
				m_canTakeDamage = pawn->CanBeDamaged();
				pawn->SetCanBeDamaged(false);
				playerController->SetIgnoreMoveInput(true);
				playerController->SetIgnoreLookInput(true);
			}
			pawn->DisableInput(playerController);
		}
	}
	m_screenWidget->AddToViewport(10000);
	if (m_titleInCombat) { _world.GetTimerManager().SetTimer(m_loadingCheckTimer, this, &USciFiScreenSubsystem::CheckCombatPreloadComplete, 0.1f, true); }
	TWeakObjectPtr<USciFiScreenSubsystem> weakThis(this);
	TWeakObjectPtr<UWorld> weakWorld(&_world);
	_world.GetTimerManager().SetTimerForNextTick(
		[weakThis, weakWorld]()
		{
			if (weakThis.IsValid() && weakWorld.IsValid())
			{
				weakThis->RemoveLegacyScreenWidgets(*weakWorld.Get());
				if (APlayerController *controller = weakWorld->GetFirstPlayerController())
				{
					if (weakThis->m_menuStage) { weakThis->m_menuStage->CopyRifle(controller->GetPawn()); }
					if (weakThis->m_titleInCombat)
					{
						//プレイヤー初期化後にタイトル用の入力設定を確定する
						controller->SetInputMode(FInputModeUIOnly());
						controller->bShowMouseCursor = true;
						controller->SetIgnoreMoveInput(true);
						controller->SetIgnoreLookInput(true);
						if (USciFiAudioSubsystem *audio = weakWorld->GetSubsystem<USciFiAudioSubsystem>()) { audio->PlayScreenMusic(TEXT("GameTitle")); }
					}
				}
				//旧ウィジェットを閉じてから、新しいメニューへ決定ボタンのフォーカスを戻す
				if (weakThis->m_menuStage && weakThis->m_screenWidget) { weakThis->m_screenWidget->FocusPrimaryAction(); }
			}
		});
	TWeakObjectPtr<USciFiScreenWidget> weakScreen(m_screenWidget);
	//時間の開始と解除を管理するTimer Handle
	FTimerHandle restoreScreenTimer;
	_world.GetTimerManager().SetTimer(
		restoreScreenTimer,
		[weakScreen, isCombatLoading]()
		{
			if (weakScreen.IsValid() && !weakScreen->IsInViewport()) { weakScreen->AddToViewport(10000); }
			if (weakScreen.IsValid() && !isCombatLoading) { weakScreen->FocusPrimaryAction(); }
		},
		0.5f, false);
	if (isCombatLoading)
	{
		m_loadingStartedAt = _world.GetTimeSeconds();
		playerController->bShowMouseCursor = false;
		playerController->SetIgnoreMoveInput(true);
		playerController->SetIgnoreLookInput(true);
		_world.GetTimerManager().SetTimer(m_loadingCheckTimer, this, &USciFiScreenSubsystem::CheckCombatPreloadComplete, 0.10f, true, 0.10f);
		return;
	}

	playerController->bShowMouseCursor = true;
	FInputModeUIOnly inputMode;
	inputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	playerController->SetInputMode(inputMode);
	//タイトル・リザルト画面をゲームパッドだけで操作できるようPrimaryボタンへ初期フォーカスを置く。
	m_screenWidget->FocusPrimaryAction();
}

//戦闘 Preload 完了を検査し、進行不能や不正状態を検出する関数
void USciFiScreenSubsystem::CheckCombatPreloadComplete()
{
	UWorld *world = GetWorld();
	//Worldが無効な場合は画面遷移とGame Modeに合うUIを表示できないため終了する
	if (!world) { return; }
	if (m_titleInCombat && !m_arrivalStarted) { HideHud(); return; }
	if (m_arrivalStarted)
	{
		if (IsValid(m_menuStage) && m_menuStage->GetArrivalProgress() >= 0.75f && m_hudStartedAt < 0.0)
		{
			m_hudStartedAt = world->GetTimeSeconds();
			FadeHud();
			world->GetTimerManager().SetTimer(m_hudTimer, this, &USciFiScreenSubsystem::FadeHud, 0.016f, true);
		}
		if (!IsValid(m_menuStage) || m_menuStage->IsMoveFinished()) { FinishCombatPreload(); }
		return;
	}

	//戦闘空間の処理件数を集計する変数
	int32 arenaCount = 0;
	//現在Mapに対応するScreen UIを表示するために使用するb_allPrepared
	bool allPrepared = true;
	//現在Mapに対応するScreen UIを表示するため、対象要素を順番に更新する
	for (TActorIterator<ACombatArena> iterator(world); iterator; ++iterator)
	{
		++arenaCount;
		allPrepared &= iterator->IsEncounterPreloadComplete();
	}
	const bool minimumDisplayElapsed = world->GetTimeSeconds() - m_loadingStartedAt >= 1.0;
	if (arenaCount > 0 && allPrepared && minimumDisplayElapsed)
	{
		if (m_playArrival && BeginArrival()) { return; }
		FinishCombatPreload();
	}
}

//戦闘 Preloadを終了し、専用タイマーと一時フラグを解除する関数
void USciFiScreenSubsystem::FinishCombatPreload()
{
	UWorld *world = GetWorld();
	//Worldが無効な場合は画面遷移とGame Modeに合うUIを表示できないため終了する
	if (!world) { return; }
	world->GetTimerManager().ClearTimer(m_loadingCheckTimer);
	if (!m_arrivalStarted || m_hudStartedAt < 0.0) { RestoreHud(); }
	if (m_arrivalStarted || m_titleInCombat)
	{
		if (APlayerController *controller = world->GetFirstPlayerController())
		{
			APawn *player = controller->GetPawn();
			if (player)
			{
				player->SetCanBeDamaged(m_canTakeDamage);
				player->EnableInput(controller);
				controller->SetViewTarget(player);
				controller->bAutoManageActiveCameraTarget = true;
			}
		}
		if (IsValid(m_menuStage)) { m_menuStage->Destroy(); }
		m_menuStage = nullptr;
	}
	if (m_screenWidget)
	{
		m_screenWidget->RemoveFromParent();
		m_screenWidget = nullptr;
	}
	//プレイヤーを制御するControllerが使用可能な場合だけ命令を送る
	if (APlayerController *playerController = world->GetFirstPlayerController())
	{
		FInputModeGameOnly inputMode;
		inputMode.SetConsumeCaptureMouseDown(false);
		playerController->SetInputMode(inputMode);
		playerController->ResetIgnoreMoveInput();
		playerController->ResetIgnoreLookInput();
		playerController->bShowMouseCursor = false;
	}
	if (m_titleInCombat)
	{
		if (USciFiAudioSubsystem *audio = world->GetSubsystem<USciFiAudioSubsystem>()) { audio->PlayScreenMusic(TEXT("MainLevel")); }
	}
	//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
	if (APlayerChara *player = Cast<APlayerChara>(UGameplayStatics::GetPlayerPawn(world, 0))) { player->PrepareForArenaTransition(); }
}

//旧画面WidgetをViewportから除去して新旧UIの二重表示を防ぐ関数
void USciFiScreenSubsystem::RemoveLegacyScreenWidgets(UWorld &_world)
{
	static const TCHAR *legacyWidgetPaths[] = {TEXT("/Game/UI/WBP_GameOver.WBP_GameOver_C"), TEXT("/Game/UI/WBP_GameClear.WBP_GameClear_C"),
											   TEXT("/Game/UI/WBPTitleWidget.WBPTitleWidget_C")};

	//現在Mapに対応するScreen UIを表示するため、Widgetを順番に更新する
	for (const TCHAR *classPath : legacyWidgetPaths)
	{
		//生成Classとして生成するClass参照
		UClass *legacyClass = LoadClass<UUserWidget>(nullptr, classPath);
		//無効な生成Classを除外して残りの要素だけを処理する
		if (!legacyClass) continue;

		//Widgetを一括処理するために収集する配列
		TArray<UUserWidget *> legacyWidgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(&_world, legacyWidgets, legacyClass, false);
		//現在Mapに対応するScreen UIを表示するため、Widgetを順番に更新する
		for (UUserWidget *legacyWidget : legacyWidgets)
		{
			//無効な有効性を除外して残りの要素だけを処理する
			if (!IsValid(legacyWidget)) continue;
			legacyWidget->RemoveFromParent();
		}
	}
}

//サブシステム終了時に再生中の音声、タイマー、ウィジェット参照を解放する関数
void USciFiScreenSubsystem::Deinitialize()
{
	RestoreHud();
	if (UWorld *world = GetWorld())
	{
		world->GetTimerManager().ClearTimer(m_loadingCheckTimer);
		world->GetTimerManager().ClearTimer(m_travelTimer);
		world->GetTimerManager().ClearTimer(m_hudTimer);
	}
	if (m_screenWidget)
	{
		m_screenWidget->RemoveFromParent();
		m_screenWidget = nullptr;
	}
	Super::Deinitialize();
}

void USciFiScreenSubsystem::StartGameTransition()
{
	if (m_departing || !GetWorld()) { return; }
	m_departing = true;
	if (m_screenWidget) { m_screenWidget->SetCinematic(true); }
	if (APlayerController *controller = GetWorld()->GetFirstPlayerController())
	{
		controller->bShowMouseCursor = false;
		controller->SetInputMode(FInputModeGameOnly());
		if (m_menuStage) { m_menuStage->CopyRifle(controller->GetPawn()); }
	}
	if (!IsValid(m_menuStage) || !m_menuStage->StartDeparture())
	{
		OpenCombatLevel();
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(m_travelTimer, this, &USciFiScreenSubsystem::CheckDeparture, 0.05f, true);
}

void USciFiScreenSubsystem::CheckDeparture()
{
	if (IsValid(m_menuStage) && !m_menuStage->IsMoveFinished()) { return; }
	if (m_titleInCombat)
	{
		//敵の事前生成を待つ間も、背面カメラと本編の背景を表示し続ける
		for (TActorIterator<ACombatArena> arena(GetWorld()); arena; ++arena)
		{
			if (!arena->IsEncounterPreloadComplete()) { return; }
		}
		GetWorld()->GetTimerManager().ClearTimer(m_travelTimer);
		if (!BeginArrival()) { FinishCombatPreload(); }
		return;
	}
	GetWorld()->GetTimerManager().ClearTimer(m_travelTimer);
	if (APlayerController *controller = GetWorld()->GetFirstPlayerController())
	{
		if (controller->PlayerCameraManager) { controller->PlayerCameraManager->StartCameraFade(0.f, 1.f, 0.3f, FLinearColor::Black, false, true); }
	}
	//暗転が完了してから同期ロードへ渡し、止まった歩行姿勢を見せない
	GetWorld()->GetTimerManager().SetTimer(m_travelTimer, this, &USciFiScreenSubsystem::OpenCombatLevel, 0.4f, false);
}

void USciFiScreenSubsystem::OpenCombatLevel()
{
	if (!GetWorld()) { return; }
	if (UMenuTravelState *travel = GetWorld()->GetGameInstance()->GetSubsystem<UMenuTravelState>()) { travel->m_playArrival = true; }
	UGameplayStatics::OpenLevel(this, TEXT("MainLevel"));
}

bool USciFiScreenSubsystem::BeginArrival()
{
	APlayerController *controller = GetWorld()->GetFirstPlayerController();
	APlayerChara *player = controller ? Cast<APlayerChara>(controller->GetPawn()) : nullptr;
	if (!player || !m_screenWidget) { return false; }
	if (m_titleInCombat && IsValid(m_menuStage))
	{
		m_menuStage->ContinueArrival();
		m_arrivalStarted = true;
		return true;
	}
	FActorSpawnParameters spawn;
	spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	spawn.ObjectFlags |= RF_Transient;
	m_menuStage = GetWorld()->SpawnActor<AMenuCharacterStage>(player->GetActorLocation(), player->GetActorRotation(), spawn);
	if (!m_menuStage || !m_menuStage->StartArrival(player))
	{
		if (m_menuStage) { m_menuStage->Destroy(); }
		m_menuStage = nullptr;
		return false;
	}
	m_arrivalStarted = true;
	m_canTakeDamage = player->CanBeDamaged();
	player->SetCanBeDamaged(false);
	if (player->GetCharacterMovement()) { player->GetCharacterMovement()->StopMovementImmediately(); }
	controller->bAutoManageActiveCameraTarget = false;
	controller->SetViewTarget(m_menuStage);
	if (controller->PlayerCameraManager) { controller->PlayerCameraManager->StartCameraFade(1.f, 0.f, 0.35f, FLinearColor::Black); }
	HideHud();
	m_screenWidget->SetCinematic(true);
	return true;
}

void USciFiScreenSubsystem::HideHud()
{
	TArray<UUserWidget *> widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(), widgets, UUserWidget::StaticClass(), true);
	for (UUserWidget *widget : widgets)
	{
		if (widget == m_screenWidget || m_hiddenWidgets.Contains(widget)) { continue; }
		m_hiddenWidgets.Add(widget, widget->GetVisibility());
		m_hudOpacity.Add(widget, widget->GetRenderOpacity());
		m_hudPosition.Add(widget, widget->GetRenderTransform().Translation);
		widget->SetVisibility(ESlateVisibility::Hidden);
	}
}

void USciFiScreenSubsystem::RestoreHud()
{
	for (const auto &entry : m_hiddenWidgets)
	{
		if (entry.Key.IsValid())
		{
			entry.Key->SetVisibility(entry.Value);
			if (UPlayerUI *hud = Cast<UPlayerUI>(entry.Key.Get())) { hud->SetIntroProgress(1.f); }
			if (const float *opacity = m_hudOpacity.Find(entry.Key)) { entry.Key->SetRenderOpacity(*opacity); }
			if (const FVector2D *position = m_hudPosition.Find(entry.Key)) { entry.Key->SetRenderTranslation(*position); }
		}
	}
	m_hiddenWidgets.Empty();
	m_hudOpacity.Empty();
	m_hudPosition.Empty();
}

void USciFiScreenSubsystem::FadeHud()
{
	if (!GetWorld()) { return; }
	//FPSへの移行が終わる前に出し始め、操作開始後まで短く余韻を残す
	const float ratio = FMath::Clamp(static_cast<float>((GetWorld()->GetTimeSeconds() - m_hudStartedAt) / 1.25), 0.f, 1.f);
	const float blend = ratio * ratio * (3.f - 2.f * ratio);
	for (const auto &entry : m_hiddenWidgets)
	{
		if (!entry.Key.IsValid()) { continue; }
		if (UPlayerUI *hud = Cast<UPlayerUI>(entry.Key.Get()))
		{
			//クロスヘアは固定し、左右の表示だけをそれぞれの端から出す
			hud->SetIntroProgress(blend);
			hud->SetVisibility(entry.Value);
			continue;
		}
		entry.Key->SetRenderOpacity(m_hudOpacity.FindRef(entry.Key) * blend);
		entry.Key->SetRenderTranslation(m_hudPosition.FindRef(entry.Key) + FVector2D(-100.f * (1.f - blend), 0.f));
		entry.Key->SetVisibility(entry.Value);
	}
	if (m_screenWidget) { m_screenWidget->SetRenderOpacity(1.f - blend); }
	if (ratio >= 1.f)
	{
		RestoreHud();
		GetWorld()->GetTimerManager().ClearTimer(m_hudTimer);
	}
}
