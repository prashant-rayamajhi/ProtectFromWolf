#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyMeleeWeapon.generated.h"

class UBoxComponent;

UCLASS()
//敵の剣に追従する当たり判定と一振り一回のプレイヤーダメージを管理する武器Actor
class PROTECTFROMWOLF_API AMeleeWeapon : public AActor
{
	GENERATED_BODY()

  public:
	//敵の近接武器が与えるダメージと当たり判定を初期化する関数
	AMeleeWeapon();

  protected:
	//Overlapイベントを登録し、開始時の当たり判定を無効化する関数
	virtual void BeginPlay() override;

  public:
	//攻撃モンタージュの命中区間でOverlap判定を有効化し、命中履歴を初期化する関数
	void ActivateWeapon();

	//攻撃モンタージュの命中区間終了時にOverlap判定を無効化する関数
	void DeactivateWeapon();

	//プレイヤーへ一振りで与えるダメージ量を変更する関数
	void SetDamage(float _damage);

	//剣へ重なったプレイヤーを検証し、一振り一回だけダメージを適用する
	UFUNCTION()
	void OnWeaponOverlap(UPrimitiveComponent *_overlappedComp, AActor *_otherActor, UPrimitiveComponent *_otherComp, int32 _otherBodyIndex,
						 bool _bFromSweep, const FHitResult &_sweepResult);

  protected:
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
