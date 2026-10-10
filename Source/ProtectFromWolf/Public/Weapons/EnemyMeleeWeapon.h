#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyMeleeWeapon.generated.h"

class UBoxComponent;
class UAnimMontage;

UCLASS()
//敵の剣に追従する当たり判定と一振り一回のプレイヤーダメージを管理する武器Actor
class PROTECTFROMWOLF_API AMeleeWeapon : public AActor
{
	GENERATED_BODY()
	friend class FEnemyBladeSweepTest;
	friend struct FEnemyVisualReviewHarness;

  public:
	//敵の近接武器が与えるダメージと当たり判定を初期化する関数
	AMeleeWeapon();

  protected:
	//Overlapイベントを登録し、開始時の当たり判定を無効化する関数
	virtual void BeginPlay() override;
	//骨の更新後に前フレームからの刃の移動経路を調べる関数
	virtual void Tick(float _deltaTime) override;

  public:
	//攻撃モンタージュの命中区間でOverlap判定を有効化し、命中履歴を初期化する関数
	void ActivateWeapon();

	//攻撃モンタージュの命中区間終了時にOverlap判定を無効化する関数
	void DeactivateWeapon(bool _finishSwing = false);
	//一振りの再生区間と命中時間を記録し、描画されなかった剣の軌道も検査する関数
	void SetSwingAnimation(UAnimMontage *_montage, float _sectionStart, float _sectionEnd, float _startRatio, float _endRatio);
	//アニメーションが次のコマへ進む前に命中を確定してコンボの分岐へ渡す関数
	void UpdateSwing();

	//プレイヤーへ一振りで与えるダメージ量を変更する関数
	void SetDamage(float _damage);

	//剣へ重なったプレイヤーを検証し、一振り一回だけダメージを適用する
	UFUNCTION()
	void OnWeaponOverlap(UPrimitiveComponent *_overlappedComp, AActor *_otherActor, UPrimitiveComponent *_otherComp, int32 _otherBodyIndex,
						 bool _bFromSweep, const FHitResult &_sweepResult);

  protected:
	//刃の前回位置と現在位置の間を調べ、接触イベントの取り逃しを補う関数
	void SweepBlade();
	//二つの刃の姿勢を結ぶ経路で接触を検出する関数
	void SweepBladePath(const FTransform &_from, const FTransform &_to);
	//未検査のアニメーション区間を細かく区切り剣の通過を検査する関数
	void SampleSwing(bool _finishSwing = false);
	//アニメーションの指定時刻から剣の判定位置を復元する関数
	bool GetAnimatedBlade(float _position, FTransform &_blade) const;
	//命中軌道を参照する攻撃モンタージュ
	TWeakObjectPtr<UAnimMontage> m_swingMontage;
	//一振りが始まったゲーム内時刻
	double m_swingStart = 0.0;
	//今回の一振りのモンタージュ開始位置
	float m_sectionStart = 0.f;
	//今回の一振りで命中を受け付ける最初の位置
	float m_hitStart = 0.f;
	//今回の一振りで命中を受け付ける最後の位置
	float m_hitEnd = 0.f;
	//剣の経路を検査済みの再生位置
	float m_samplePosition = 0.f;
	//接触と経路検査で共通の一振り一回のダメージを適用する関数
	void ApplyBladeHit(AActor *_target);
	//前フレームの刃の位置と回転
	FTransform m_lastBlade;
	//命中受付中だけ刃の移動経路を検査する状態
	bool m_swingActive = false;
	//m_weaponMeshは、敵の手ソケットへ装着して表示する剣メッシュ参照
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	UStaticMeshComponent *m_weaponMesh;

	//m_collisionBoxは、剣の刃に追従してOverlapを検出する当たり判定参照
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon")
	UBoxComponent *m_collisionBox;

	//m_damageは、命中したプレイヤーへ一回適用するダメージ量
	float m_damage;

	//m_bHasHitは、現在の一振りですでに有効な命中が発生したかを示すフラグ
	bool m_hasHit;

	//m_hitActorsは、現在の一振りでダメージ適用済みのActor配列
	UPROPERTY()
	TArray<AActor *> m_hitActors;
};
