

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyBullet.generated.h"

//敵弾の表示と移動と軌跡に使用するコンポーネントの宣言
class UStaticMeshComponent;
class USphereComponent;
class UProjectileMovementComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UCombatTracer;

UCLASS()
//敵弾の飛翔、衝突、プレイヤーダメージを管理する弾丸
class PROTECTFROMWOLF_API AEnemyBullet : public AActor
{
	GENERATED_BODY()

  public:
	//弾速、寿命、当たり判定の初期値を設定する関数
	AEnemyBullet();

  protected:
	//敵弾を一本の細い光で示す表示部品
	UPROPERTY(VisibleAnywhere, Category = "Effects")
	UCombatTracer *m_tracer;
	//生成時に移動コンポーネントと衝突通知を有効にする関数
	virtual void BeginPlay() override;
	//弾の消滅時に味方へ登録した移動衝突の除外を解除する関数
	virtual void EndPlay(const EEndPlayReason::Type _reason) override;

	//当たり判定関数
	UFUNCTION()
	void OnHit(UPrimitiveComponent *_hitComp, AActor *_otherActor, UPrimitiveComponent *_otherComp, FVector _normalImpulse, const FHitResult &_hit);

  protected:
	//StaticMeshComponentの作成と初期化
	UPROPERTY(VisibleAnywhere, Category = "Collision")
	USphereComponent *m_collisionSphere;

	//敵弾の中心を発光表示するMesh
	UPROPERTY(VisibleAnywhere)
	UStaticMeshComponent *m_bulletMesh;

	//ProjectileMovementComponentの作成と初期化
	UPROPERTY(VisibleAnywhere)
	UProjectileMovementComponent *m_projectileMovement;

	//敵弾の進行方向と軌道を示すNiagara Trail
	UPROPERTY(VisibleAnywhere, Category = "Effects")
	UNiagaraComponent *m_beamTrail;

	//弾丸がワールドまたはキャラクターへ衝突した地点に一度だけ生成する命中エフェクト
	UPROPERTY(EditDefaultsOnly, Category = "Effects")
	UNiagaraSystem *m_impactEffect;

  private:
	//この弾を移動時の障害物から除外した味方のカプセル
	TArray<TWeakObjectPtr<class UPrimitiveComponent>> m_ignoredBodies;
	//着弾処理済みかどうか
	bool m_hasHit = false;

  private:
	//敵弾がプレイヤーへ一発ごとに与えるダメージ量
	UPROPERTY(EditAnywhere, Category = "Damage")
	float m_damage;
};
