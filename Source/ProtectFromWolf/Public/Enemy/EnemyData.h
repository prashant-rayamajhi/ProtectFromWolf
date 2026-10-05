#pragma once

#include "CoreMinimal.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimMontage.h"
#include "Engine/DataTable.h"
#include "EnemyData.generated.h"

//敵の攻撃スタイル
UENUM(BlueprintType)
enum class EEnemyAttackStyle : uint8
{
	Melee UMETA(DisplayName = "Melee"),
	Gun UMETA(DisplayName = "Gun"),
	Laser UMETA(DisplayName = "Laser"),
	Adaptive UMETA(DisplayName = "Adaptive")
};

UENUM(BlueprintType)
enum class EEnemyRank : uint8
{
	Minion UMETA(DisplayName = "Minion"),
	MiddleBoss UMETA(DisplayName = "MiddleBoss"),
	LastBoss UMETA(DisplayName = "LastBoss")
};

//CSVの1行に対応するデータ構造
USTRUCT(BlueprintType)
struct FEnemyData : public FTableRowBase
{
	GENERATED_BODY()

  public:
	//データテーブル上で敵を識別する表示名
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString m_enemyName;

	//このデータ行から生成する敵のBlueprintクラス
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TSubclassOf<class AEnemyChara> m_enemyBpClass;

	//生成時に敵へ設定する最大体力
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	float m_maxHealth = 0.f;

	//この敵が一回の攻撃でプレイヤーへ与える基礎ダメージ
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	float m_attackPower = 0.f;

	//攻撃 範囲 を距離または移動量として調整する数値
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	float m_attackRange = 0.f;

	//移動 速度 を距離または移動量として調整する数値
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	float m_moveSpeed = 0.f;

	//Chase 範囲 を距離または移動量として調整する数値
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	float m_chaseRange = 0.f;

	//近接、銃撃、レーザーからAIが使用する攻撃方式
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	EEnemyAttackStyle m_attackStyle = EEnemyAttackStyle::Melee;

	//雑魚、中間ボス、ラストボスを区別する敵ランク
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	EEnemyRank m_enemyRank = EEnemyRank::Minion;

	FEnemyData() = default;
};

//戦闘段階ごとの敵編成をデータテーブルから読み込む構造体
USTRUCT(BlueprintType)
struct FStageData : public FTableRowBase
{
	GENERATED_BODY()
  public:
	//敵編成を適用する戦闘段階の番号
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 m_stageNum = 0;

	//この戦闘段階へ生成する雑魚敵の人数
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 m_minionCount = 0;

	//次の段階へ進むために倒す必要がある敵の合計数
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 m_killTarget = 0;

	//この戦闘段階で生成する敵データ行名の一覧
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EEnemyRank m_enemyType = EEnemyRank::Minion;

	//中間ボスに一度だけ退避行動を許可するか示す変数
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool m_retreat = false;
};

//非戦闘時と戦闘中の待機アニメーションを分けるための設定
UENUM(BlueprintType)
enum class EEnemyCombatPosture : uint8
{
	Relaxed UMETA(DisplayName = "Normal Idle"),
	Guarded UMETA(DisplayName = "Combat Idle"),
	Pressuring UMETA(DisplayName = "Pressure"),
	Evading UMETA(DisplayName = "Evade")
};
