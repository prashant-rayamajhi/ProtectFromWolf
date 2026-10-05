#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MenuCharacterStage.generated.h"

class UCameraComponent;
class USkeletalMeshComponent;
class UPointLightComponent;
class UAnimSequence;
class APlayerChara;
class APawn;
enum class ESciFiScreenMode : uint8;

//戦闘に参加しないキャラクターと照明でメニューの背景を作るクラス
UCLASS(NotBlueprintable, Transient)
class PROTECTFROMWOLF_API AMenuCharacterStage : public AActor
{
	GENERATED_BODY()
public:
	//背景専用のメッシュ、床、カメラ、照明を生成する関数
	AMenuCharacterStage();
	//画面の種類に合わせてアニメーションと照明を切り替える関数
	bool SetPresentation(ESciFiScreenMode _mode);
	//本編で生成された銃のメッシュ、材質と装着位置を背景へ反映する関数
	void CopyRifle(APawn *_player);
	//本編の背景でタイトルの待機姿勢と正面カメラを用意する関数
	bool StartMapTitle(APlayerChara *_player);
	//背面へ回り込んだカメラからFPSへの移動を続ける関数
	void ContinueArrival();
	//キャラクターの歩行に合わせてカメラを背面へ回す関数
	bool StartDeparture();
	//本編の開始位置でTPSから実際のFPSカメラへ接続する関数
	bool StartArrival(APlayerChara *_player);
	//演出用カメラと歩行位置を更新する関数
	virtual void Tick(float _delta) override;
	//終了やマップ移動でも隠した本編メッシュを元に戻す関数
	virtual void EndPlay(const EEndPlayReason::Type _reason) override;
	//カメラの移動が終わったかを返す関数
	bool IsMoveFinished() const { return m_moveFinished; }
	//FPSへ近づく終盤からHUDを出すための進行率を返す関数
	float GetArrivalProgress() const { return m_arriving ? FMath::Clamp(m_elapsed / 3.6f, 0.f, 1.f) : 0.f; }

private:
	//演出用メッシュの足元を床の高さに合わせる関数
	void AlignToFloor();
	//演出終了時に操作を引き継ぐプレイヤーの位置
	FVector m_playerEnd = FVector::ZeroVector;
	//タイトルと本編で同じ背景とキャラクターを使う状態
	bool m_inMap = false;
	//歩行中に繰り返す本編と同じアニメーション
	UPROPERTY()
	TObjectPtr<UAnimSequence> m_walk;
	//FPS視点を引き継ぐ本編のプレイヤー
	TWeakObjectPtr<APlayerChara> m_player;
	//演出中だけ非表示にする本編の銃
	TWeakObjectPtr<AActor> m_sourceGun;
	//プレイヤーの表示を元に戻すための可視状態
	bool m_meshVisible = true;
	//本編の銃を元に戻すための非表示状態
	bool m_gunHidden = false;
	//タイトル側か本編側のカメラ演出かを区別する状態
	bool m_arriving = false;
	//カメラ移動が完了した状態
	bool m_moveFinished = false;
	//演出開始からの秒数
	float m_elapsed = 0.f;
	//本編のメッシュへぴったり重なる演出終了位置
	FVector m_endLocation = FVector::ZeroVector;
	//タイトルで歩き始める位置
	FVector m_startLocation = FVector::ZeroVector;
	//本編側の演出開始時のカメラ位置
	FVector m_cameraStart = FVector::ZeroVector;
	//本編側の演出開始時のカメラ回転
	FQuat m_cameraRotation = FQuat::Identity;
	//右側のキャラクターを全身で撮るカメラ
	UPROPERTY()
	TObjectPtr<UCameraComponent> m_camera;
	//ダメージ判定や移動処理を持たない表示専用の人型メッシュ
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> m_character;
	//待機姿勢の手に取り付ける銃
	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> m_rifle;
	//顔と体の正面を照らす照明
	UPROPERTY()
	TObjectPtr<UPointLightComponent> m_keyLight;
	//体の輪郭を背景から分離する背面照明
	UPROPERTY()
	TObjectPtr<UPointLightComponent> m_rimLight;
	//タイトルとクリアで繰り返す待機アニメーション
	UPROPERTY()
	TObjectPtr<UAnimSequence> m_idle;
	//ゲームオーバーで一度だけ再生する死亡アニメーション
	UPROPERTY()
	TObjectPtr<UAnimSequence> m_death;
};
