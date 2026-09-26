#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "EnemyAIController.generated.h"

class APlayerChara;
class AEnemyChara;
class UBehaviorTree;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;

//AIの状態を管理する列挙型
UENUM(BlueprintType)
enum class EAIState : uint8
{
	Move,
	Wait,
	CombatMelee,
	CombatRangeMove,
	CombatRangeHide,
	Retreat,
	LaserPreparation
};

//敵の状態遷移と移動と戦闘判断を管理するクラス
UCLASS()
//敵の知覚情報から移動先と攻撃手段を選択するAIコントローラー
class PROTECTFROMWOLF_API AEnemyAIController : public AAIController
{
	GENERATED_BODY()

  public:
	//敵の認識、移動、攻撃を管理するAI Controllerを初期化する関数
	AEnemyAIController();

	//毎フレーム呼ばれる更新関数
	virtual void Tick(float _deltaTime) override;

	//敵キャラクターを操作し始めた際の関数
	virtual void OnPossess(APawn *_pawn) override;

	bool IsUsingBehaviorTree() const
	{
		return m_usingBehaviorTree;
	}

	//ダメージを受けた際の通知関数
	void OnEnemyDamaged();

	//プレイヤーが発した物音の位置と強度を知覚記憶へ登録する関数
	void NotifyPlayerNoise(AActor *_player, const FVector &_noiseLocation);

	//戦闘空間へ入ったプレイヤーを即座に攻撃対象へ設定してBehavior Treeを再開する関数
	void PrimeForArenaCombat(AActor *_player);
	//Recent プレイヤー 物音が現在成立しているかを判定する
	bool HasRecentPlayerNoise(float _memorySeconds = 6.f) const;
	//視認したプレイヤーの位置と視認状態を知覚記憶へ反映する関数
	void UpdateVisualContact(AActor *_player, bool _canSee, const FVector &_location);
	bool HasActiveVisualContact() const
	{
		return m_hasActiveVisualContact;
	}
	//直近の視覚接触があるか判定する関数
	bool HasRecentVisualContact(float _memorySeconds = 5.f) const;
	//最後の判明しているプレイヤー位置を取得する関数
	FVector GetLastKnownPlayerLocation() const;

  protected:
	//状態処理を更新する関数
	void UpdateStateLogic();

	//巡回を更新する関数
	void UpdatePatrol(float _deltaTime);

	//近接戦闘中の更新関数
	void UpdateCombatMelee(float _deltaTime);

	//遠距離戦闘中の更新関数
	void UpdateCombatRange(float _deltaTime);

	//退避中の更新関数
	void UpdateRetreat(float _deltaTime);

	//レーザー準備中の更新関数
	void UpdateLaserPreparation(float _deltaTime);

	//武器の状態を更新する関数
	void UpdateWeaponState();

  private:
	//隠れる場所を探す関数
	bool FindCoverSpot(FVector &_outCoverSpot);

	//隠れる場所が低いかどうかを判定する関数
	bool IsLowCover(FVector _coverPosition);

	//プレイヤーが視界内にいるか確認する関数
	bool CheckLineOfSight();

  protected:
	//ターゲットとなるアクター
	AActor *m_targetActor;

	//操作対象の敵キャラクター
	AEnemyChara *m_enemy;

	//巡回と追跡と戦闘を切り替える現在のAI状態
	EAIState m_currentState;

	//各ステートで使用するタイマー
	float m_stateTimer;

	//目標となる隠れ場所の座標
	FVector m_targetCoverPos;

	//レーザー攻撃時に退避する座標
	FVector m_laserRetreatPos;

  private:
	//Behavior Tree停止や長時間静止を監視し、停止した敵を戦闘判断へ復帰させる関数
	void TickBehaviorTreeSafety(float _deltaTime);

	//視覚または聴覚の知覚通知から攻撃対象と最終確認位置を更新する関数
	UFUNCTION()
	void HandleTargetPerceptionUpdated(AActor *_actor, FAIStimulus _stimulus);

	//敵AIの判断処理を実行するBehavior Tree Asset
	UPROPERTY(EditDefaultsOnly, Category = "AI|Behavior Tree")
	TSoftObjectPtr<UBehaviorTree> m_behaviorTreeAsset;

	//プレイヤーの視覚と聴覚刺激を受信する知覚コンポーネント
	UPROPERTY(VisibleAnywhere, Category = "AI|Perception")
	UAIPerceptionComponent *m_aiPerception;

	//敵AIの視野角、視認距離、見失う距離を定義する視覚設定
	UPROPERTY()
	UAISenseConfig_Sight *m_sightConfig;

	//銃声と足音を検知する距離と記憶時間を定義する聴覚設定
	UPROPERTY()
	UAISenseConfig_Hearing *m_hearingConfig;

	//行動判断をBehavior Treeへ任せているか示す変数
	bool m_usingBehaviorTree;

	//適応近接攻撃距離
	float m_adaptiveMeleeDist;

	//適応射程後方距離
	float m_adaptiveRangeBackDist;

	//プレイヤーを見失ってから追跡を継続する時間
	float m_hideDuration;

	//プレイヤーまで射線が通るか定期確認するタイマー
	float m_losCheckTimer;

	//負荷を抑えながら射線を再確認する時間間隔
	float m_losCheckInterval;

	//最後の可能視認プレイヤーかどうか
	bool m_lastCanSeePlayer;

	//武器構え済みかどうか
	bool m_weaponDrawn;

	//遠距離敵が次の横移動を選ぶまでの待機タイマー
	float m_strafeTimer;

	//遠距離敵が同じ位置へ留まり続けないための横移動間隔
	float m_strafeInterval;

	//現在の横移動方向
	float m_currentStrafeDir;

	//攻撃再使用待ち時間かどうか
	bool m_isAttackCooldown;

	//次の攻撃判断を許可するまでの待機時間を管理するタイマー
	FTimerHandle m_attackCooldownTimerHandle;

	//ボスモードタイマーかどうか
	float m_bossModeTimer;

	//近接攻撃モードかどうか
	bool m_isMeleeMode;

	//実行中終了後弾薬レーザーかどうか
	bool m_doingPostAmmoLaser;

	//最後のプレイヤー音時刻
	float m_lastPlayerNoiseTime;
	//最後のプレイヤー音位置
	FVector m_lastPlayerNoiseLocation;
	//最後の視覚接触時刻
	float m_lastVisualContactTime;
	//最後の視覚接触位置
	FVector m_lastVisualContactLocation;
	//保持有効視覚接触かどうか
	bool m_hasActiveVisualContact;
	//次の知覚更新時刻
	float m_btPerceptionRefreshTime;
	//次の待機復帰時刻
	float m_btIdleRecoveryTime;
};
