#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MeleeComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
//所有キャラクター前方の近接範囲を検査し、命中対象へ一回ダメージを与える部品
class PROTECTFROMWOLF_API UMeleeComponent : public UActorComponent
{
	GENERATED_BODY()

  public:
	//人間形態の近接攻撃距離と再使用待ち時間を初期化する関数
	UMeleeComponent();

  protected:
	//ゲーム開始時に近接攻撃を行う所有プレイヤーを取得する関数
	virtual void BeginPlay() override;

  public:
	//装備中の近接武器で攻撃モーションを開始する関数
	void MeleeAttack();

	//ダメージを設定する関数
	void SetDamage(float _damage);

	//攻撃射程を設定する関数
	void SetAttackRange(float _range);

  private:
	//人間形態の近接攻撃が対象へ届く最大距離
	float m_attackRange;

	//連続入力による過剰な攻撃を防ぐ再使用待ち時間
	float m_meleeInterval;

	//人間形態の近接攻撃が対象へ与えるダメージ量
	float m_meleeDamage;

	//近接攻撃の再使用待ち時間を終えているか示す変数
	bool m_canBeMelee;
};
