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
//パンチ、パンチ、キックの三段すべてを再生できるか調べる関数
bool HasCompleteCombo(const TArray<UAnimMontage *> &_montages)
{
	return _montages.Num() >= 3 && IsValid(_montages[0]) && IsValid(_montages[1]) && IsValid(_montages[2]);
}

//狼男用の三段コンボを決められた順序で読み込む関数
void LoadDefaultCombo(TArray<UAnimMontage *> &_montages)
{
	static const TCHAR *montagePaths[] = {TEXT("/Game/chara/CharaAnimation/Animation/Wolf/AM_Attack1.AM_Attack1"),
										  TEXT("/Game/chara/CharaAnimation/Animation/Wolf/AM_Attack2.AM_Attack2"),
										  TEXT("/Game/chara/CharaAnimation/Animation/Wolf/AM_Attack3.AM_Attack3")};

	_montages.Reset();
	for (const TCHAR *montagePath : montagePaths)
	{
		if (UAnimMontage *montage = LoadObject<UAnimMontage>(nullptr, montagePath)) { _montages.Add(montage); }
	}
}

//Blueprintで未設定の狼男メッシュ、アニメーション、攻撃を補う関数
void LoadDefaultFormAssets(FWerewolfFormSettings &_settings)
{
	if (!_settings.m_mesh)
	{
		_settings.m_mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/chara/charamodel/Wolf/SkeletonTransformation.SkeletonTransformation"));
	}
	if (!_settings.m_animationClass)
	{
		_settings.m_animationClass = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/chara/CharaAnimation/ABP_Wolf.ABP_Wolf_C"));
	}
	if (!HasCompleteCombo(_settings.m_comboMontages)) { LoadDefaultCombo(_settings.m_comboMontages); }
}
}

//人間形態と狼男形態のメッシュ・能力切替を管理する部品の既定値とコンポーネント構成を初期化する関数
UWerewolfFormComponent::UWerewolfFormComponent()
	: m_hasHumanSize(false), m_humanCapsuleHalfHeight(0.f), m_humanCapsuleRadius(0.f), m_humanMeshTransform(FTransform::Identity)
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
	if (_mesh && HasCompleteCombo(m_settings.m_comboMontages) && _mesh->GetSkeleton() == m_settings.m_comboMontages[0]->GetSkeleton())
	{
		m_settings.m_mesh = _mesh;
	}
	if (_animationClass && _animationClass == m_settings.m_animationClass) { m_settings.m_animationClass = _animationClass; }
	const bool legacyComboMatchesMesh = HasCompleteCombo(_comboMontages) && m_settings.m_mesh &&
										  Algo::AllOf(_comboMontages, [this](const UAnimMontage *_montage)
													  { return _montage && _montage->GetSkeleton() == m_settings.m_mesh->GetSkeleton(); });
	if (legacyComboMatchesMesh) { m_settings.m_comboMontages = _comboMontages; }
	else if (!HasCompleteCombo(m_settings.m_comboMontages))
	{
		//古い二モンタージュ構成を現在の三段コンボ構成へ補正する処理
		LoadDefaultCombo(m_settings.m_comboMontages);
	}

	if (_walkSpeed > 0.f) m_settings.m_walkSpeed = _walkSpeed;
	if (_cameraArmLength >= 0.f) m_settings.m_cameraArmLength = _cameraArmLength;
}

//狼男Mesh、能力補正、TPS Cameraを有効にして変身を完了する関数
bool UWerewolfFormComponent::EnterForm(APlayerChara *_player)
{
	//プレイヤーが無効な場合は狼男の変身時間とComboを更新できないため終了する
	if (!IsValid(_player) || !IsValid(m_settings.m_mesh) || !m_settings.m_animationClass) { return false; }
	if (!HasCompleteCombo(m_settings.m_comboMontages) || m_settings.m_mesh->GetSkeleton() != m_settings.m_comboMontages[0]->GetSkeleton())
	{
		return false;
	}

	USkeletalMeshComponent *meshComponent = _player->GetMesh();
	UCapsuleComponent *capsuleComponent = _player->GetCapsuleComponent();
	UCharacterMovementComponent *movement = _player->GetCharacterMovement();
	//Meshが無効な場合は狼男の変身時間とComboを更新できないため終了する
	if (!meshComponent || !capsuleComponent || !movement) { return false; }

	//対象が有効な場合だけ狼男の変身時間とComboを更新する
	//解除時に元の大きさへ戻せるよう、人型の寸法は最初の変身時だけ記録する
	if (!m_hasHumanSize)
	{
		m_humanCapsuleHalfHeight = capsuleComponent->GetUnscaledCapsuleHalfHeight();
		m_humanCapsuleRadius = capsuleComponent->GetUnscaledCapsuleRadius();
		m_humanMeshTransform = meshComponent->GetRelativeTransform();
		m_hasHumanSize = true;
	}

	const FBoxSphereBounds wolfBounds = m_settings.m_mesh->GetBounds();
	const float wolfCapsuleHalfHeight = FMath::Max(m_humanCapsuleHalfHeight, FMath::CeilToFloat(wolfBounds.BoxExtent.Z));
	const float wolfCapsuleRadius =
		FMath::Clamp(FMath::CeilToFloat(FMath::Max(wolfBounds.BoxExtent.X, wolfBounds.BoxExtent.Y)), m_humanCapsuleRadius, wolfCapsuleHalfHeight);
	const float previousHalfHeight = capsuleComponent->GetUnscaledCapsuleHalfHeight();
	const float halfHeightDelta = wolfCapsuleHalfHeight - previousHalfHeight;

	if (halfHeightDelta > KINDA_SMALL_NUMBER)
	{
		_player->AddActorWorldOffset(FVector(0.f, 0.f, halfHeightDelta), true, nullptr, ETeleportType::None);
	}
	capsuleComponent->SetCapsuleSize(wolfCapsuleRadius, wolfCapsuleHalfHeight, true);
	if (halfHeightDelta < -KINDA_SMALL_NUMBER)
	{
		_player->AddActorWorldOffset(FVector(0.f, 0.f, halfHeightDelta), true, nullptr, ETeleportType::None);
	}

	meshComponent->SetSkeletalMesh(m_settings.m_mesh);
	FVector wolfMeshRelativeLocation = m_humanMeshTransform.GetLocation();
	//狼男メッシュのインポート原点を基準に、最初の相対位置をCapsule下端へ合わせる。
	wolfMeshRelativeLocation.Z = wolfBounds.BoxExtent.Z - wolfBounds.Origin.Z - wolfCapsuleHalfHeight;
	meshComponent->SetRelativeLocation(wolfMeshRelativeLocation);
	if (m_settings.m_animationClass) { meshComponent->SetAnimInstanceClass(m_settings.m_animationClass); }
	//AnimBP適用後の実ポーズBoundsを更新し、足元とCapsule下端の差を解消する。
	//SkeletonTransformationは参照Boundsと実ポーズBoundsの下端が約100cm異なるため、
	//参照Boundsだけで相対位置を決めると変身直後の狼男が空中に浮く。
	meshComponent->TickAnimation(0.f, false);
	meshComponent->RefreshBoneTransforms();
	meshComponent->UpdateBounds();
	const float capsuleBottom = capsuleComponent->GetComponentLocation().Z - wolfCapsuleHalfHeight;
	const float uncorrectedMeshBottom = meshComponent->Bounds.Origin.Z - meshComponent->Bounds.BoxExtent.Z;
	//狼男メッシュは尻尾などが足より下のBoundsを作るため、接地判定には左右の足ボーンを優先する。
	const bool hasLeftFoot = meshComponent->DoesSocketExist(TEXT("LeftFoot"));
	const bool hasRightFoot = meshComponent->DoesSocketExist(TEXT("RightFoot"));
	float footAnchorZ = uncorrectedMeshBottom;
	if (hasLeftFoot || hasRightFoot)
	{
		const float leftFootZ = hasLeftFoot ? meshComponent->GetSocketLocation(TEXT("LeftFoot")).Z : BIG_NUMBER;
		const float rightFootZ = hasRightFoot ? meshComponent->GetSocketLocation(TEXT("RightFoot")).Z : BIG_NUMBER;
		footAnchorZ = FMath::Min(leftFootZ, rightFootZ);
	}
	const float footAlignmentOffset = capsuleBottom - footAnchorZ;
	if (!FMath::IsNearlyZero(footAlignmentOffset, 0.5f))
	{
		wolfMeshRelativeLocation.Z += footAlignmentOffset;
		meshComponent->SetRelativeLocation(wolfMeshRelativeLocation);
		meshComponent->UpdateBounds();
	}

	movement->MaxWalkSpeed = m_settings.m_walkSpeed;
	movement->JumpZVelocity = m_settings.m_jumpVelocity;
	return true;
}

//人型のメッシュ、当たり判定、移動速度とジャンプ力へ戻す関数
void UWerewolfFormComponent::ExitForm(APlayerChara *_player, USkeletalMesh *_defaultMesh, TSubclassOf<UAnimInstance> _defaultAnimationClass,
									  float _defaultWalkSpeed, float _defaultJumpVelocity)
{
	//復元するメッシュが無効なら、現在の姿を消さずに終了する
	if (!IsValid(_player) || !IsValid(_defaultMesh)) { return; }

	USkeletalMeshComponent *meshComponent = _player->GetMesh();
	UCapsuleComponent *capsuleComponent = _player->GetCapsuleComponent();
	UCharacterMovementComponent *movement = _player->GetCharacterMovement();
	//復元に必要な部品が揃う前に、姿や当たり判定を変更しない
	if (!meshComponent || !capsuleComponent || !movement) { return; }

	if (m_hasHumanSize)
	{
		const float previousHalfHeight = capsuleComponent->GetUnscaledCapsuleHalfHeight();
		capsuleComponent->SetCapsuleSize(m_humanCapsuleRadius, m_humanCapsuleHalfHeight, true);
		const float halfHeightDelta = m_humanCapsuleHalfHeight - previousHalfHeight;
		//対象が有効な場合だけ狼男の変身時間とComboを更新する
		if (!FMath::IsNearlyZero(halfHeightDelta))
		{
			_player->AddActorWorldOffset(FVector(0.f, 0.f, halfHeightDelta), true, nullptr, ETeleportType::None);
		}
	}

	meshComponent->SetSkeletalMesh(_defaultMesh);
	if (m_hasHumanSize) { meshComponent->SetRelativeTransform(m_humanMeshTransform); }
	if (_defaultAnimationClass) { meshComponent->SetAnimInstanceClass(_defaultAnimationClass); }
	meshComponent->UpdateBounds();

	movement->MaxWalkSpeed = _defaultWalkSpeed;
	movement->JumpZVelocity = _defaultJumpVelocity;
}

//ComboMontageの現在値を参照側へ渡す関数
UAnimMontage *UWerewolfFormComponent::GetComboMontage(int32 _index) const
{
	return m_settings.m_comboMontages.IsValidIndex(_index) ? m_settings.m_comboMontages[_index] : nullptr;
}
