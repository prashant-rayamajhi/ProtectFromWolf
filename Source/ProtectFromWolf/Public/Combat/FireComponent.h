#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FireComponent.generated.h"

class AEnemyBullet;

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
//敵所有者の前方射線ダメージ、敵弾生成、射撃間隔を管理する旧式射撃部品
class PROTECTFROMWOLF_API UFireComponent : public UActorComponent
{
	GENERATED_BODY()

  public:
	//射撃間隔と弾道判定に使用する初期値を設定する関数
	UFireComponent();

  protected:
	//ゲーム開始時に所有キャラクターと弾生成位置を取得する関数
	virtual void BeginPlay() override;

  public:
	//ダメージを設定する関数
	void SetDamage(float _damage)
	{
		m_damage = _damage;
	}

	//射撃間隔を確認し、前方トレースのダメージと視認用敵弾を一回生成する関数
	UFUNCTION()
	void FireBullet();

  public:
	//m_pBulletは、敵の正面へ生成する視認用敵弾クラス
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bullet")
	TSubclassOf<AEnemyBullet> m_bullet;

	//発射済みでワールド内を飛翔している敵弾を寿命管理する配列
	UPROPERTY()
	TArray<AEnemyBullet *> m_activeBullet;

  private:
	//射撃を初期状態へ戻す関数
	void ResetFire();

	//弾を消去する関数
	void ClearBullet();

  private:
	//次の射撃を許可するまでの時間を管理するタイマー
	FTimerHandle m_resetFire;

	//次の射撃を許可するまでに必要な時間間隔
	float m_fireRate;

	//この射撃コンポーネントが一発ごとに与えるダメージ量
	float m_damage;

	//射撃間隔を終えて次の弾を発射できるか示す変数
	bool m_canBeFired;
};
