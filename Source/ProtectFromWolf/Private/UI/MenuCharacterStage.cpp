#include "UI/MenuCharacterStage.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UI/SciFiScreenWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "Player/PlayerChara.h"
#include "Weapons/Gun.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

AMenuCharacterStage::AMenuCharacterStage()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	USceneComponent *root = CreateDefaultSubobject<USceneComponent>(TEXT("StageRoot"));
	SetRootComponent(root);
	m_character = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MenuCharacter"));
	m_character->SetupAttachment(root);
	m_character->SetRelativeLocation(FVector(0.f, -120.f, 0.f));
	m_character->SetRelativeRotation(FRotator(0.f, -70.f, 0.f));
	m_character->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	m_character->SetGenerateOverlapEvents(false);
	m_character->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	m_rifle = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MenuRifle"));
	m_rifle->SetupAttachment(m_character, TEXT("IdleWeaponSocket"));
	m_rifle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	m_rifle->SetGenerateOverlapEvents(false);

	//コンストラクタでアセットを参照し、パッケージ化でも依存関係を追えるようにする
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> human(TEXT("/Game/chara/charamodel/Player/Ch15_nonPBR.Ch15_nonPBR"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> rifle(TEXT("/Game/chara/Weapons/SKM_AR_Rifle_Player.SKM_AR_Rifle_Player"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> idle(TEXT("/Game/chara/CharaAnimation/Animation/Player/NormalIdle.NormalIdle"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> death(TEXT("/Game/chara/CharaAnimation/Animation/Player/Rifle_Death.Rifle_Death"));
	if (human.Succeeded()) { m_character->SetSkeletalMesh(human.Object); }
	if (rifle.Succeeded()) { m_rifle->SetSkeletalMesh(rifle.Object); }
	m_idle = idle.Object;
	m_death = death.Object;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> walk(TEXT("/Game/chara/CharaAnimation/Animation/Player/Rifle_Walk.Rifle_Walk"));
	m_walk = walk.Object;

	m_camera = CreateDefaultSubobject<UCameraComponent>(TEXT("MenuCamera"));
	m_camera->SetupAttachment(root);
	m_camera->SetRelativeLocation(FVector(520.f, 0.f, 145.f));
	m_camera->SetRelativeRotation((FVector(0.f, 0.f, 95.f) - m_camera->GetRelativeLocation()).Rotation());
	m_camera->FieldOfView = 43.f;
	m_camera->bConstrainAspectRatio = false;
	//照明の明暗で露出が変動せず、キャラクターと文字を落ち着いて見られるようにする
	m_camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
	m_camera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	m_camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	m_camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	m_camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	m_camera->PostProcessSettings.AutoExposureBias = -2.5f;
	m_camera->PostProcessSettings.bOverride_BloomIntensity = true;
	m_camera->PostProcessSettings.BloomIntensity = 0.12f;
	m_camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	m_camera->PostProcessSettings.VignetteIntensity = 0.35f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> metal(TEXT("/Game/StarterContent/Materials/M_Metal_Steel.M_Metal_Steel"));
	//実際の足元に影を落とし、キャラクターが背景から浮いて見えるのを防ぐ
	UStaticMeshComponent *floor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StageFloor"));
	floor->SetupAttachment(root);
	floor->SetStaticMesh(cube.Object);
	floor->SetRelativeLocation(FVector(0.f, 0.f, -5.f));
	floor->SetRelativeScale3D(FVector(16.f, 16.f, 0.1f));
	floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (metal.Succeeded()) { floor->SetMaterial(0, metal.Object); }
	UStaticMeshComponent *wall = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StageWall"));
	wall->SetupAttachment(root);
	wall->SetStaticMesh(cube.Object);
	wall->SetRelativeLocation(FVector(-230.f, 0.f, 260.f));
	wall->SetRelativeScale3D(FVector(0.1f, 16.f, 6.f));
	wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (metal.Succeeded()) { wall->SetMaterial(0, metal.Object); }

	m_keyLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("KeyLight"));
	m_keyLight->SetupAttachment(root);
	m_keyLight->SetRelativeLocation(FVector(200.f, -30.f, 250.f));
	m_keyLight->IntensityUnits = ELightUnits::Lumens;
	m_keyLight->SetIntensity(8500.f);
	m_keyLight->SetAttenuationRadius(1000.f);
	m_keyLight->SetSourceRadius(45.f);
	m_rimLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RimLight"));
	m_rimLight->SetupAttachment(root);
	m_rimLight->SetRelativeLocation(FVector(-110.f, -230.f, 200.f));
	m_rimLight->IntensityUnits = ELightUnits::Lumens;
	m_rimLight->SetIntensity(6500.f);
	m_rimLight->SetAttenuationRadius(750.f);
	m_rimLight->SetSourceRadius(35.f);
}

bool AMenuCharacterStage::SetPresentation(ESciFiScreenMode _mode)
{
	const bool isOver = _mode == ESciFiScreenMode::GameOver;
	const bool isClear = _mode == ESciFiScreenMode::GameClear;
	UAnimSequence *animation = isOver ? m_death.Get() : m_idle.Get();
	if (!m_character->GetSkeletalMeshAsset() || !animation) { return false; }
	if (animation->GetSkeleton() != m_character->GetSkeletalMeshAsset()->GetSkeleton()) { return false; }

	//ゲーム用AnimBPではなく、表示用メッシュでSequenceを再生する
	m_character->PlayAnimation(animation, !isOver);
	if (UAnimInstance *instance = m_character->GetAnimInstance()) { instance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion); }
	m_rifle->SetVisibility(!isOver && m_character->DoesSocketExist(TEXT("IdleWeaponSocket")));
	m_keyLight->SetLightColor(isClear ? FLinearColor(1.f, 0.83f, 0.59f) : FLinearColor(0.72f, 0.84f, 1.f));
	m_rimLight->SetLightColor(isOver ? FLinearColor(1.f, 0.12f, 0.055f) : FLinearColor(0.1f, 0.65f, 1.f));
	m_keyLight->SetIntensity(isOver ? 3800.f : 8500.f);
	//死亡姿勢は床に近くなるため、カメラも低くして画面右側に収める
	if (isOver)
	{
		//倒れた足が左側のボタンに隠れないよう、右寄りに配置する
		m_character->SetRelativeLocation(FVector(0.f, -175.f, 0.f));
		m_camera->SetRelativeLocation(FVector(520.f, 0.f, 220.f));
		m_camera->SetRelativeRotation((FVector(0.f, 0.f, 55.f) - m_camera->GetRelativeLocation()).Rotation());
		m_camera->FieldOfView = 53.f;
	}
	return true;
}

void AMenuCharacterStage::CopyRifle(APawn *_player)
{
	if (!_player) { return; }
	TArray<AActor *> attached;
	_player->GetAttachedActors(attached);
	for (AActor *actor : attached)
	{
		AGun *gun = Cast<AGun>(actor);
		if (!gun || !gun->m_gunMesh) { continue; }
		USkeletalMeshComponent *source = gun->m_gunMesh;
		m_rifle->SetSkeletalMesh(source->GetSkeletalMeshAsset());
		for (int32 index = 0; index < source->GetNumMaterials(); ++index) { m_rifle->SetMaterial(index, source->GetMaterial(index)); }
		const FName socket = source->GetAttachSocketName();
		if (m_character->DoesSocketExist(socket))
		{
			m_rifle->AttachToComponent(m_character, FAttachmentTransformRules::SnapToTargetIncludingScale, socket);
			m_rifle->SetRelativeTransform(source->GetRelativeTransform());
		}
		if (m_player.IsValid())
		{
			//本体の銃を隠し、演出用キャラクターの銃だけを表示する
			if (m_sourceGun.Get() != actor) { m_gunHidden = actor->IsHidden(); }
			actor->SetActorHiddenInGame(true);
		}
		m_sourceGun = actor;
		return;
	}
}

bool AMenuCharacterStage::StartDeparture()
{
	if (!m_walk || !m_character->GetSkeletalMeshAsset()) { return false; }
	if (m_walk->GetSkeleton() != m_character->GetSkeletalMeshAsset()->GetSkeleton()) { return false; }
	m_arriving = false;
	m_elapsed = 0.f;
	m_moveFinished = false;
	m_startLocation = m_character->GetRelativeLocation();
	if (m_inMap)
	{
		m_cameraStart = m_camera->GetComponentLocation();
		m_cameraRotation = m_camera->GetComponentQuat();
		m_character->PlayAnimation(m_walk, true);
		SetActorTickEnabled(true);
		return true;
	}
	m_cameraStart = m_camera->GetRelativeLocation();
	m_cameraRotation = m_camera->GetRelativeRotation().Quaternion();
	//固定カメラの真下まで歩く方向へ体を向ける
	m_endLocation = FVector(m_cameraStart.X, m_cameraStart.Y, m_startLocation.Z);
	const float heading = (m_endLocation - m_startLocation).Rotation().Yaw;
	m_character->SetRelativeRotation(FRotator(0.f, heading - 90.f, 0.f));
	m_character->PlayAnimation(m_walk, true);
	SetActorTickEnabled(true);
	return true;
}

bool AMenuCharacterStage::StartArrival(APlayerChara *_player)
{
	if (!_player || !_player->GetMesh() || !m_walk) { return false; }
	USkeletalMeshComponent *mesh = _player->GetMesh();
	if (!mesh->GetSkeletalMeshAsset() || mesh->GetSkeletalMeshAsset()->GetSkeleton() != m_walk->GetSkeleton()) { return false; }
	m_player = _player;
	m_arriving = true;
	m_elapsed = 0.f;
	m_moveFinished = false;
	SetActorTransform(_player->GetActorTransform());
	m_character->SetSkeletalMesh(mesh->GetSkeletalMeshAsset());
	m_character->SetRelativeTransform(mesh->GetRelativeTransform());
	for (int32 index = 0; index < mesh->GetNumMaterials(); ++index) { m_character->SetMaterial(index, mesh->GetMaterial(index)); }
	m_endLocation = m_character->GetRelativeLocation();
	m_character->SetRelativeLocation(m_endLocation - FVector(100.f, 0.f, 0.f));
	m_startLocation = m_character->GetRelativeLocation();
	m_character->PlayAnimation(m_walk, true);
	CopyRifle(_player);
	m_meshVisible = mesh->IsVisible();
	mesh->SetVisibility(false);
	//本編の背景を使い、メニュー用の床と壁、照明は表示しない
	TArray<UStaticMeshComponent *> scenery;
	GetComponents(scenery);
	for (UStaticMeshComponent *part : scenery) { part->SetVisibility(false); }
	m_keyLight->SetVisibility(false);
	m_rimLight->SetVisibility(false);
	m_camera->PostProcessBlendWeight = 0.f;
	m_cameraStart = _player->GetActorLocation() - _player->GetActorForwardVector() * 340.f + FVector(0.f, 0.f, 100.f);
	m_cameraRotation = (_player->GetActorLocation() + FVector(0.f, 0.f, 40.f) - m_cameraStart).ToOrientationQuat();
	m_camera->SetWorldLocationAndRotation(m_cameraStart, m_cameraRotation);
	m_camera->FieldOfView = 65.f;
	SetActorTickEnabled(true);
	return true;
}

bool AMenuCharacterStage::StartMapTitle(APlayerChara *_player)
{
	if (!StartArrival(_player)) { return false; }
	m_inMap = true;
	m_arriving = false;
	m_character->SetRelativeLocation(m_endLocation - FVector(180.f, 0.f, 0.f));
	m_character->PlayAnimation(m_idle, true);
	AlignToFloor();
	//本編の開始領域内で前進し、最後に操作用プレイヤーへ同じ位置を引き継ぐ
	//TPSからFPSへ移る3.6秒間も毎秒90cmで前進する
	m_playerEnd = _player->GetActorLocation() + _player->GetActorForwardVector() * 624.f;
	//本編の開始地点を正面から撮り、メニューの右側に人物を置く
	const FVector focus = m_character->GetComponentLocation() + FVector(0.f, 0.f, 105.f);
	const FVector camera = focus + _player->GetActorForwardVector() * 540.f + _player->GetActorRightVector() * 140.f + FVector(0.f, 0.f, 35.f);
	m_camera->SetWorldLocationAndRotation(camera, (focus + _player->GetActorRightVector() * 105.f - camera).Rotation());
	m_camera->FieldOfView = 55.f;
	SetActorTickEnabled(false);
	return true;
}

void AMenuCharacterStage::ContinueArrival()
{
	if (m_inMap && m_player.IsValid())
	{
		//壁がある場合は手前で止め、カメラを実際に到達できた位置へ接続する
		m_player->SetActorLocation(m_playerEnd, true);
		m_endLocation = GetActorTransform().InverseTransformPosition(m_player->GetMesh()->GetComponentLocation());
	}
	//視点を置き直さず、周回が終わった位置と姿勢を引き継ぐ
	m_arriving = true;
	m_elapsed = 0.f;
	m_moveFinished = false;
	m_startLocation = m_character->GetRelativeLocation();
	m_cameraStart = m_camera->GetComponentLocation();
	m_cameraRotation = m_camera->GetComponentQuat();
	SetActorTickEnabled(true);
}

void AMenuCharacterStage::Tick(float _delta)
{
	Super::Tick(_delta);
	m_elapsed += _delta;
	const float duration = m_arriving ? 3.6f : (m_inMap ? 4.5f : 4.2f);
	const float ratio = FMath::Clamp(m_elapsed / duration, 0.f, 1.f);
	const float blend = ratio * ratio * (3.f - 2.f * ratio);
	if (!m_arriving)
	{
		if (m_inMap && m_player.IsValid())
		{
			//前半で三メートル近づき、後半も歩き続けながら背面へ回る
			const float travel = FMath::Min(m_elapsed, 2.5f) * 120.f + FMath::Max(m_elapsed - 2.5f, 0.f) * 90.f;
			m_character->SetRelativeLocation(m_startLocation + FVector(FMath::Min(travel, 480.f), 0.f, 0.f));
			AlignToFloor();
			//歩き出しは正面を保ち、近づいてから横を通って背面へ回り込む
			const float orbit = FMath::Clamp((m_elapsed - 2.5f) / 2.f, 0.f, 1.f);
			const float turn = orbit * orbit * (3.f - 2.f * orbit);
			const FVector focus = m_character->GetComponentLocation() + FVector(0.f, 0.f, 105.f);
			const FVector offset = m_cameraStart - (GetActorTransform().TransformPosition(m_startLocation + FVector(300.f, 0.f, 0.f)) + FVector(0.f, 0.f, 105.f));
			const float angle = FMath::Lerp(FMath::Atan2(FVector::DotProduct(offset, m_player->GetActorRightVector()), FVector::DotProduct(offset, m_player->GetActorForwardVector())), PI, turn);
			const float distance = FMath::Lerp(offset.Size2D(), 340.f, turn);
			const FVector direction = m_player->GetActorForwardVector() * FMath::Cos(angle) + m_player->GetActorRightVector() * FMath::Sin(angle);
			const FVector camera = focus + direction * distance + FVector(0.f, 0.f, FMath::Lerp(35.f, 55.f, turn));
			//二地点の直線補間で人物へ食い込まず、一定の距離を保った円弧で回り込む
			m_camera->SetWorldLocation(orbit > 0.f ? camera : m_cameraStart);
			//カメラの位置は接近まで固定し、人物が画面外へ外れないよう視線だけ追従する
			const float framing = FMath::Clamp(m_elapsed / 1.2f, 0.f, 1.f);
			const FVector lookAt = focus + m_player->GetActorRightVector() * 105.f * (1.f - framing);
			m_camera->SetWorldRotation((lookAt - m_camera->GetComponentLocation()).Rotation());
			m_camera->FieldOfView = FMath::Lerp(55.f, 65.f, turn);
		}
		else
		{
		//カメラと画角を変えず、人物がレンズを覆う位置まで一定速度で歩かせる
		m_character->SetRelativeLocation(FMath::Lerp(m_startLocation, m_endLocation, ratio));
		}
	}
	else if (m_player.IsValid())
	{
		//最後のFPS位置は固定値ではなく、本編カメラの現在設定から取得する
		FMinimalViewInfo view;
		m_player->CalcCamera(_delta, view);
		//歩行は一定速度で進め、カメラの加減速とは分離する
		m_character->SetRelativeLocation(FMath::Lerp(m_startLocation, m_endLocation, ratio));
		AlignToFloor();
		const FVector walk = GetActorTransform().TransformVector(m_endLocation - m_startLocation);
		//前進する人物を追いながら、背面から目の位置まで近づく
		const FVector follow = m_cameraStart + walk * ratio;
		const FVector eye = view.Location - walk * (1.f - ratio);
		m_camera->SetWorldLocation(FMath::Lerp(follow, eye, blend));
		m_camera->SetWorldRotation(FQuat::Slerp(m_cameraRotation, view.Rotation.Quaternion(), blend));
		m_camera->FieldOfView = FMath::Lerp(65.f, view.FOV, blend);
		//頭の内側を通過する瞬間だけ表示用の頭を隠す
		if (ratio > 0.8f) { m_character->HideBoneByName(TEXT("Head"), EPhysBodyOp::PBO_None); }
	}
	if (ratio >= 1.f)
	{
		m_moveFinished = true;
		SetActorTickEnabled(false);
	}
}

void AMenuCharacterStage::AlignToFloor()
{
	if (!GetWorld() || !m_character->GetSkeletalMeshAsset()) { return; }
	//待機と歩行の上下動は残し、メッシュ原点の高さだけを床に合わせる
	const FVector position = m_character->GetComponentLocation();
	FCollisionQueryParams query;
	query.AddIgnoredActor(this);
	if (m_player.IsValid()) { query.AddIgnoredActor(m_player.Get()); }
	if (m_sourceGun.IsValid()) { query.AddIgnoredActor(m_sourceGun.Get()); }
	FHitResult floor;
	if (!GetWorld()->LineTraceSingleByChannel(floor, position + FVector(0.f, 0.f, 150.f), position - FVector(0.f, 0.f, 500.f), ECC_WorldStatic, query)) { return; }
	const FBoxSphereBounds bounds = m_character->GetSkeletalMeshAsset()->GetBounds().TransformBy(m_character->GetComponentTransform());
	const float bottom = bounds.Origin.Z - bounds.BoxExtent.Z;
	m_character->AddWorldOffset(FVector(0.f, 0.f, floor.ImpactPoint.Z - bottom));
}

void AMenuCharacterStage::EndPlay(const EEndPlayReason::Type _reason)
{
	if (m_player.IsValid())
	{
		m_player->GetMesh()->SetVisibility(m_meshVisible);
		if (m_sourceGun.IsValid()) { m_sourceGun->SetActorHiddenInGame(m_gunHidden); }
	}
	Super::EndPlay(_reason);
}
