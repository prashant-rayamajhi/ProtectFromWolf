#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WerewolfFormComponent.generated.h"

class APlayerChara;
class UAnimInstance;
class UAnimMontage;
class USkeletalMesh;

USTRUCT(BlueprintType)
struct PROTECTFROMWOLF_API FWerewolfFormSettings
{
	GENERATED_BODY()

	//狼男へ変身した時にプレイヤーへ設定するメッシュ
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	USkeletalMesh *m_mesh = nullptr;

	//狼男の移動と攻撃を再生するAnimation Blueprintクラス
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Visual")
	TSubclassOf<UAnimInstance> m_animationClass;

	//Walk 速度 を距離または移動量として調整する数値
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float m_walkSpeed = 1500.f;

	//Jump Velocity を距離または移動量として調整する数値
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float m_jumpVelocity = 850.f;

	//カメラ Arm Length を距離または移動量として調整する数値
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera", meta = (ClampMin = "0.0"))
	float m_cameraArmLength = 450.f;

	//狼男の背中を基準にTPS Cameraを回転させるPivot位置
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector m_cameraPivotOffset = FVector(0.f, 0.f, 160.f);

	//PivotからTPS Cameraまでの位置補正
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector m_cameraSocketOffset = FVector(0.f, 60.f, 20.f);

	//狼男TPSカメラで許可する上下回転角度の範囲
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera")
	FVector2D m_cameraPitchLimits = FVector2D(-45.f, 55.f);

	//狼男形態で入力受付に従って連続再生する近接コンボモンタージュ配列
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	TArray<UAnimMontage *> m_comboMontages;

	//変身時に失った体力から回復する最大体力割合
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float m_healthRecoveryPercent = 0.25f;

	//狼男形態で受けるDamageへ適用する倍率
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float m_incomingDamageMultiplier = 0.75f;

	//狼男形態の近接攻撃Damageへ適用する倍率
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ClampMin = "1.0"))
	float m_attackDamageMultiplier = 1.15f;
};

UCLASS(ClassGroup = (Player), meta = (BlueprintSpawnableComponent))
//人間形態と狼男形態のメッシュ・能力切替を管理する部品
class PROTECTFROMWOLF_API UWerewolfFormComponent : public UActorComponent
{
	GENERATED_BODY()

  public:
	//人間形態と狼男形態のメッシュ、能力、カメラ状態を初期化する関数
	UWerewolfFormComponent();

	void ApplyLegacyOverrides(USkeletalMesh *_mesh, TSubclassOf<UAnimInstance> _animationClass, const TArray<UAnimMontage *> &_comboMontages,
							  float _walkSpeed, float _cameraArmLength);

	//人間Meshから狼男Meshへ切り替えて形態補正とTPS Cameraを適用する関数
	bool EnterForm(APlayerChara *_player);
	void ExitForm(APlayerChara *_player, USkeletalMesh *_defaultMesh, TSubclassOf<UAnimInstance> _defaultAnimationClass, float _defaultWalkSpeed,
				  float _defaultJumpVelocity);

	//指定したCombo段階の狼男攻撃モンタージュを返す関数
	UAnimMontage *GetComboMontage(int32 _index) const;
	float GetCameraArmLength() const
	{
		return m_settings.m_cameraArmLength;
	}
	FVector GetCameraPivotOffset() const
	{
		return m_settings.m_cameraPivotOffset;
	}
	FVector GetCameraSocketOffset() const
	{
		return m_settings.m_cameraSocketOffset;
	}
	FVector2D GetCameraPitchLimits() const
	{
		return m_settings.m_cameraPitchLimits;
	}
	float GetHealthRecoveryPercent() const
	{
		return m_settings.m_healthRecoveryPercent;
	}
	float GetIncomingDamageMultiplier() const
	{
		return m_settings.m_incomingDamageMultiplier;
	}
	float GetAttackDamageMultiplier() const
	{
		return m_settings.m_attackDamageMultiplier;
	}

  private:
	//狼男形態のメッシュ、AnimBP、能力補正、カメラ設定をまとめた構造体
	UPROPERTY(EditAnywhere, Category = "Werewolf")
	FWerewolfFormSettings m_settings;

	//変身解除時に戻す人間形態のCollision設定を記録済みか示す状態
	bool m_hasHumanSize;
	//human Capsule Half Heightをゲーム内距離または移動量として調整する数値
	float m_humanCapsuleHalfHeight;
	//human Capsule Radiusをゲーム内距離または移動量として調整する数値
	float m_humanCapsuleRadius;
	//変身解除時にMeshの沈み込みを防ぐ人間形態の基準Transform
	FTransform m_humanMeshTransform;
};
