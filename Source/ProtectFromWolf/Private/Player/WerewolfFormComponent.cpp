#include "Player/WerewolfFormComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/PlayerChara.h"
#include "Algo/AllOf.h"

namespace
{
bool HasCompleteCombo(const TArray<UAnimMontage *> &_montages)
{
	return _montages.Num() >= 3 && IsValid(_montages[0]) && IsValid(_montages[1]) && IsValid(_montages[2]);
}

void LoadDefaultCombo(TArray<UAnimMontage *> &_montages)
{
	static const TCHAR *montagePaths[] = {TEXT("/Game/chara/CharaAnimation/Animation/Wolf/AM_Attack1.AM_Attack1"),
										  TEXT("/Game/chara/CharaAnimation/Animation/Wolf/AM_Attack2.AM_Attack2"),
										  TEXT("/Game/chara/CharaAnimation/Animation/Wolf/AM_Attack3.AM_Attack3")};

	_montages.Reset();
	//担当機能の状態を更新するため、Montageを順番に更新する
	for (const TCHAR *montagePath : montagePaths)
	{
		//担当機能の状態を更新するため、Montageの状態を確認する
		if (UAnimMontage *montage = LoadObject<UAnimMontage>(nullptr, montagePath)) { _montages.Add(montage); }
	}
}

void LoadDefaultFormAssets(FWerewolfFormSettings &_settings)
{
	//必要なMeshが有効な場合だけ後続処理を実行する
	if (!_settings.Mesh)
	{
		_settings.Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/chara/charamodel/Wolf/SkeletonTransformation.SkeletonTransformation"));
	}
	//必要なAnimationが有効な場合だけ後続処理を実行する
	if (!_settings.AnimationClass)
	{
		_settings.AnimationClass = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/chara/CharaAnimation/ABP_Wolf.ABP_Wolf_C"));
	}
	//必要なMontageが有効な場合だけ後続処理を実行する
	if (!HasCompleteCombo(_settings.ComboMontages)) { LoadDefaultCombo(_settings.ComboMontages); }
}
}

//人間形態と狼男形態のメッシュ・能力切替を管理する部品の既定値とコンポーネント構成を初期化する関数
UWerewolfFormComponent::UWerewolfFormComponent()
	: m_hasCachedHumanCollision(false), m_humanCapsuleHalfHeight(0.f), m_humanCapsuleRadius(0.f), m_humanMeshRelativeTransform(FTransform::Identity)
{
	PrimaryComponentTick.bCanEverTick = false;
}

//Legacy Overridesを対象へ反映し、反映後の状態を確定する関数
void UWerewolfFormComponent::ApplyLegacyOverrides(USkeletalMesh *_mesh, TSubclassOf<UAnimInstance> _animationClass,
												  const TArray<UAnimMontage *> &_comboMontages, float _walkSpeed, float _cameraArmLength)
{
	//Blueprintの生成完了後に狼男用アセットを安全に読み込む処理
	LoadDefaultFormAssets(m_settings);

	//古いPlayerCharaBPの設定で現在の狼男メッシュとAnimBlueprintが置き換わることを防ぐ処理
	if (_mesh && m_settings.ComboMontages.Num() > 0 && _mesh->GetSkeleton() == m_settings.ComboMontages[0]->GetSkeleton())
	{
		m_settings.Mesh = _mesh;
	}
	//担当機能の状態を更新するため、Animationの状態を確認する
	if (_animationClass && _animationClass == m_settings.AnimationClass) { m_settings.AnimationClass = _animationClass; }
	const bool b_legacyComboMatchesMesh = HasCompleteCombo(_comboMontages) && m_settings.Mesh &&
										  Algo::AllOf(_comboMontages, [this](const UAnimMontage *montage)
													  { return montage && montage->GetSkeleton() == m_settings.Mesh->GetSkeleton(); });
	//担当機能の状態を更新するため、Meshの状態を確認する
	if (b_legacyComboMatchesMesh) { m_settings.ComboMontages = _comboMontages; }
	//担当機能の状態を更新するため、Montageの状態を確認する
	//必要なMontageが有効な場合だけ後続処理を実行する
	else if (!HasCompleteCombo(m_settings.ComboMontages))
	{
		//古い二モンタージュ構成を現在の三段コンボ構成へ補正する処理
		LoadDefaultCombo(m_settings.ComboMontages);
	}

	//必要なMontageが有効な場合だけ後続処理を実行する
	//担当機能の状態を更新するため、移動速度の状態を確認する
	if (_walkSpeed > 0.f) m_settings.WalkSpeed = _walkSpeed;
	//担当機能の状態を更新するため、カメラの状態を確認する
	if (_cameraArmLength >= 0.f) m_settings.CameraArmLength = _cameraArmLength;
}

//狼男Mesh、能力補正、TPS Cameraを有効にして変身を完了する関数
bool UWerewolfFormComponent::EnterForm(APlayerChara *_player)
{
	//プレイヤーが無効な場合は狼男の変身時間とComboを更新できないため終了する
	if (!_player || !m_settings.Mesh) { return false; }
	//必要なMontageが有効な場合だけ後続処理を実行する
	if (!HasCompleteCombo(m_settings.ComboMontages) || m_settings.Mesh->GetSkeleton() != m_settings.ComboMontages[0]->GetSkeleton())
	{
		return false;
	}

	USkeletalMeshComponent *meshComponent = _player->GetMesh();
	UCapsuleComponent *capsuleComponent = _player->GetCapsuleComponent();
	//Meshが無効な場合は狼男の変身時間とComboを更新できないため終了する
	if (!meshComponent || !capsuleComponent) { return false; }

	//対象が有効な場合だけ狼男の変身時間とComboを更新する
	if (!m_hasCachedHumanCollision)
	{
		m_humanCapsuleHalfHeight = capsuleComponent->GetUnscaledCapsuleHalfHeight();
		m_humanCapsuleRadius = capsuleComponent->GetUnscaledCapsuleRadius();
		m_humanMeshRelativeTransform = meshComponent->GetRelativeTransform();
		m_hasCachedHumanCollision = true;
	}

	const FBoxSphereBounds wolfBounds = m_settings.Mesh->GetBounds();
	const float wolfCapsuleHalfHeight = FMath::Max(m_humanCapsuleHalfHeight, FMath::CeilToFloat(wolfBounds.BoxExtent.Z));
	const float wolfCapsuleRadius =
		FMath::Clamp(FMath::CeilToFloat(FMath::Max(wolfBounds.BoxExtent.X, wolfBounds.BoxExtent.Y)), m_humanCapsuleRadius, wolfCapsuleHalfHeight);
	const float previousHalfHeight = capsuleComponent->GetUnscaledCapsuleHalfHeight();
	//halfHeightDeltaとして狼男の変身状態と攻撃を制御する変数
	const float halfHeightDelta = wolfCapsuleHalfHeight - previousHalfHeight;

	//担当機能の状態を更新するため、KINDA_SMALL_NUMBERの状態を確認する
	if (halfHeightDelta > KINDA_SMALL_NUMBER)
	{
		_player->AddActorWorldOffset(FVector(0.f, 0.f, halfHeightDelta), true, nullptr, ETeleportType::None);
	}
	capsuleComponent->SetCapsuleSize(wolfCapsuleRadius, wolfCapsuleHalfHeight, true);
	//担当機能の状態を更新するため、KINDA_SMALL_NUMBERの状態を確認する
	if (halfHeightDelta < -KINDA_SMALL_NUMBER)
	{
		_player->AddActorWorldOffset(FVector(0.f, 0.f, halfHeightDelta), true, nullptr, ETeleportType::None);
	}

	meshComponent->SetSkeletalMesh(m_settings.Mesh);
	FVector wolfMeshRelativeLocation = m_humanMeshRelativeTransform.GetLocation();
	//狼男メッシュのインポート原点を基準に、最初の相対位置をCapsule下端へ合わせる。
	wolfMeshRelativeLocation.Z = wolfBounds.BoxExtent.Z - wolfBounds.Origin.Z - wolfCapsuleHalfHeight;
	meshComponent->SetRelativeLocation(wolfMeshRelativeLocation);
	//担当機能の状態を更新するため、Animationの状態を確認する
	if (m_settings.AnimationClass) { meshComponent->SetAnimInstanceClass(m_settings.AnimationClass); }
	//AnimBP適用後の実ポーズBoundsを更新し、足元とCapsule下端の差を解消する。
	//SkeletonTransformationは参照Boundsと実ポーズBoundsの下端が約100cm異なるため、
	//参照Boundsだけで相対位置を決めると変身直後の狼男が空中に浮く。
	meshComponent->TickAnimation(0.f, false);
	meshComponent->RefreshBoneTransforms();
	meshComponent->UpdateBounds();
	const float capsuleBottom = capsuleComponent->GetComponentLocation().Z - wolfCapsuleHalfHeight;
	const float uncorrectedMeshBottom = meshComponent->Bounds.Origin.Z - meshComponent->Bounds.BoxExtent.Z;
	//狼男メッシュは尻尾などが足より下のBoundsを作るため、接地判定には左右の足ボーンを優先する。
	const bool b_hasLeftFoot = meshComponent->DoesSocketExist(TEXT("LeftFoot"));
	const bool b_hasRightFoot = meshComponent->DoesSocketExist(TEXT("RightFoot"));
	//footAnchorZとして狼男の変身状態と攻撃を制御する変数
	float footAnchorZ = uncorrectedMeshBottom;
	//担当機能の状態を更新するため、b_hasRightFootの状態を確認する
	if (b_hasLeftFoot || b_hasRightFoot)
	{
		const float leftFootZ = b_hasLeftFoot ? meshComponent->GetSocketLocation(TEXT("LeftFoot")).Z : BIG_NUMBER;
		const float rightFootZ = b_hasRightFoot ? meshComponent->GetSocketLocation(TEXT("RightFoot")).Z : BIG_NUMBER;
		footAnchorZ = FMath::Min(leftFootZ, rightFootZ);
	}
	//補正値として狼男の変身状態と攻撃を制御する変数
	const float footAlignmentOffset = capsuleBottom - footAnchorZ;
	//必要な補正値が有効な場合だけ後続処理を実行する
	if (!FMath::IsNearlyZero(footAlignmentOffset, 0.5f))
	{
		wolfMeshRelativeLocation.Z += footAlignmentOffset;
		meshComponent->SetRelativeLocation(wolfMeshRelativeLocation);
		meshComponent->UpdateBounds();
	}
	const float correctedMeshBottom = meshComponent->Bounds.Origin.Z - meshComponent->Bounds.BoxExtent.Z;
	const float correctedLeftFootZ = b_hasLeftFoot ? meshComponent->GetSocketLocation(TEXT("LeftFoot")).Z : correctedMeshBottom;
	const float correctedRightFootZ = b_hasRightFoot ? meshComponent->GetSocketLocation(TEXT("RightFoot")).Z : correctedMeshBottom;

	UCharacterMovementComponent *movement = _player->GetCharacterMovement();
	movement->MaxWalkSpeed = m_settings.WalkSpeed;
	movement->JumpZVelocity = m_settings.JumpVelocity;
	return true;
}

//Formを終了し、専用タイマーと一時フラグを解除する関数
void UWerewolfFormComponent::ExitForm(APlayerChara *_player, USkeletalMesh *_defaultMesh, TSubclassOf<UAnimInstance> _defaultAnimationClass,
									  float _defaultWalkSpeed, float _defaultJumpVelocity)
{
	//プレイヤーが無効な場合は狼男の変身時間とComboを更新できないため終了する
	if (!_player) { return; }

	USkeletalMeshComponent *meshComponent = _player->GetMesh();
	UCapsuleComponent *capsuleComponent = _player->GetCapsuleComponent();
	//Meshが無効な場合は狼男の変身時間とComboを更新できないため終了する
	if (!meshComponent || !capsuleComponent) { return; }

	//担当機能の状態を更新するため、m_hasCachedHumanCollisionの状態を確認する
	if (m_hasCachedHumanCollision)
	{
		const float previousHalfHeight = capsuleComponent->GetUnscaledCapsuleHalfHeight();
		capsuleComponent->SetCapsuleSize(m_humanCapsuleRadius, m_humanCapsuleHalfHeight, true);
		//halfHeightDeltaとして狼男の変身状態と攻撃を制御する変数
		const float halfHeightDelta = m_humanCapsuleHalfHeight - previousHalfHeight;
		//対象が有効な場合だけ狼男の変身時間とComboを更新する
		if (!FMath::IsNearlyZero(halfHeightDelta))
		{
			_player->AddActorWorldOffset(FVector(0.f, 0.f, halfHeightDelta), true, nullptr, ETeleportType::None);
		}
	}

	meshComponent->SetSkeletalMesh(_defaultMesh);
	//担当機能の状態を更新するため、m_hasCachedHumanCollisionの状態を確認する
	if (m_hasCachedHumanCollision) { meshComponent->SetRelativeTransform(m_humanMeshRelativeTransform); }
	//担当機能の状態を更新するため、Animationの状態を確認する
	if (_defaultAnimationClass) { meshComponent->SetAnimInstanceClass(_defaultAnimationClass); }
	meshComponent->UpdateBounds();

	UCharacterMovementComponent *movement = _player->GetCharacterMovement();
	movement->MaxWalkSpeed = _defaultWalkSpeed;
	movement->JumpZVelocity = _defaultJumpVelocity;
}

//ComboMontageの現在値を参照側へ渡す関数
UAnimMontage *UWerewolfFormComponent::GetComboMontage(int32 _index) const
{
	return m_settings.ComboMontages.IsValidIndex(_index) ? m_settings.ComboMontages[_index] : nullptr;
}
