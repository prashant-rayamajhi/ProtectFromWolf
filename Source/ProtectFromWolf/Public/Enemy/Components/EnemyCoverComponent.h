#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnemyCoverComponent.generated.h"

UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
//射線とNavMeshを評価して敵の遮蔽物利用を支援する部品
class PROTECTFROMWOLF_API UEnemyCoverComponent : public UActorComponent
{
	GENERATED_BODY()

  public:
	//遠距離敵の遮蔽物検索と射撃位置評価を初期化する関数
	UEnemyCoverComponent();
	//横移動射撃中の照準と中断条件を毎フレーム確認する関数
	virtual void TickComponent(float _deltaTime, ELevelTick _tickType, FActorComponentTickFunction *_tickFunction) override;
	//射線を保てる短い横移動または後退を選んで射撃を継続する関数
	bool TryMobileFire(AActor *_target);
	//移動射撃用に変更した速度と旋回設定を元へ戻す関数
	void StopMobileFire();
	bool IsMobileFiring() const { return m_mobileFire; }
	//退避走行を開始し、上半身のリロードと同時に脚を動かす関数
	void BeginReloadRun() { UpdateCoverRun(true); }
	//退避中の旋回を照準処理が上書きしないよう走行状態を返す関数
	bool IsRetreatRunning() const { return m_coverRun; }

	//反撃可能な遮蔽物を選び、リロード時だけ射程外への退避も許可する関数
	bool FindBestCover(AActor *_threat, FVector &_outLocation, bool _forReload = false);
	//用途と銃の射程から遮蔽物までの距離が適切か判定する関数
	bool CanUseCoverAtDistance(float _distance, bool _forReload = false) const;
	//回避 位置を距離、射線、NavMesh、戦況から選択する
	bool FindDodgeLocation(AActor *_threat, FVector &_outLocation, bool _ignoreCooldown = false);
	//Repositionが現在成立しているかを判定する関数
	bool CanReposition() const;
	//再配置の実行時刻を記録して連続移動を抑制する関数
	void CommitReposition();
	//選択した遮蔽位置を移動要求として確定する関数
	void CommitCover(const FVector &_coverLocation);
	UFUNCTION(BlueprintPure, Category = "AI|Cover")
	bool IsUsingCover() const
	{
		return m_usingCover;
	}
	UFUNCTION(BlueprintPure, Category = "AI|Cover")
	const FVector &GetCoverDestination() const
	{
		return m_coverDestination;
	}
	//距離への遮蔽物を取得する関数
	float GetDistanceToCover() const;
	//敵が選択中の遮蔽位置へ到着したか更新して返す関数
	bool UpdateAndIsAtCover();
	//Hold 遮蔽物が現在成立しているかを判定する関数
	bool ShouldHoldCover() const;
	//予約済みの遮蔽物移動要求をAIへ一度だけ渡し、要求済みフラグを解除する関数
	bool ConsumeCoverMoveRequest();
	//未処理の遮蔽移動要求を破棄する関数
	void ResetCoverMoveRequest();
	//遮蔽物 Useを終了し、タイマーと一時状態を解除する関数
	void FinishCoverUse();
	//遮蔽物の左右から射線と経路が通る射撃位置を選ぶ関数
	bool BeginPeek(AActor *_target);
	//遮蔽物から射撃位置へ移動しているか返す関数
	UFUNCTION(BlueprintPure, Category = "AI|Cover")
	bool IsPeeking() const { return m_peeking; }
	//遮蔽物を出た直後の射撃時間を確保する関数
	bool HasFiringWindow() const;
	//遮蔽への移動、顔出し、射撃後の退避を一つの順序で管理する関数
	void UpdateRangedCombat(AActor *_target, bool _visible);
	//退避位置と顔出し位置を味方が同時に使用しないよう判定する関数
	bool ReservesPosition(const FVector &_position, float _spacing) const;
	//射撃を中断して確保済みの遮蔽物へ戻す関数
	void ReturnToCover();

  private:
	//弾切れ退避で走り始め、到着や中断時に元の移動設定へ戻す関数
	void UpdateCoverRun(bool _running);
	//退避前の速度を復元するための値
	float m_beforeRunSpeed = 0.f;
	//退避中だけ設定した移動速度
	float m_runSpeed = 0.f;
	//弾切れ退避の走行設定が有効かを示す状態
	bool m_coverRun = false;
	//退避が終わった際に元へ戻す進行方向への旋回設定
	bool m_runOrient = false;
	//退避前の照準方向への旋回設定
	bool m_runDesiredRotation = false;
	//退避前のコントローラー角度への直接追従設定
	bool m_runControllerYaw = false;
	//退避終了時に自分が開始した走行モンタージュだけを停止する参照
	UPROPERTY(Transient)
	TObjectPtr<class UAnimMontage> m_runMontage;
	//移動射撃の間だけ注視する、既に発見済みの相手
	TWeakObjectPtr<AActor> m_mobileTarget;
	//射撃しながら横移動しているかを示す状態
	bool m_mobileFire = false;
	//移動射撃を終了するワールド時刻
	float m_mobileUntil = 0.f;
	//専用移動を終えた際に復元する移動速度
	float m_savedSpeed = 0.f;
	//別処理が速度を変えたか見分けるための移動射撃速度
	float m_mobileSpeed = 0.f;
	//移動方向への旋回設定を復元するための状態
	bool m_savedOrient = false;
	//コントローラーに追従する旋回設定を復元するための状態
	bool m_savedDesiredRotation = false;
	friend class FEnemyCoverCycleTest;
	friend class FEnemyCoverRunTest;
	friend class FEnemyMobileFireTest;
	//Occluded From Threatが現在成立しているかを判定する関数
	bool IsOccludedFromThreat(AActor *_threat, const FVector &_candidate) const;
	//遮蔽位置から短い経路で射撃できる左右の出口を探す関数
	bool FindPeekPosition(AActor *_target, const FVector &_origin, FVector &_position) const;
	//見失った相手は最後に確認した位置として扱う関数
	FVector GetKnownPosition(AActor *_target) const;
	//退避時に戻る遮蔽物の足元位置
	FVector m_homeCover = FVector::ZeroVector;
	//味方へ予約を公開する顔出し位置
	FVector m_peekPosition = FVector::ZeroVector;
	//同じ出口からの射撃回数を数えて位置変更の判断に使う変数
	int32 m_peekCount = 0;
	//目的地へ近づいた最後の時刻を記録して、長距離移動と停止故障を区別する変数
	float m_lastProgressTime = 0.f;
	//移動開始後に最も目的地へ近づいた距離
	float m_bestDistance = BIG_NUMBER;

	//現在位置から遮蔽物候補を検索する最大半径
	float m_searchRadius;
	//射線を外す緊急回避で左右へ移動する距離
	float m_dodgeDistance;
	//再配置再使用待ち時間
	float m_repositionCooldown;
	//最後の再配置時刻
	float m_lastRepositionTime;
	//一回の探索で評価するNavMesh上の遮蔽物候補数
	int32 m_coverSamples;
	//射線と距離の評価で選択した遮蔽物の移動先
	FVector m_coverDestination;
	//最後の遮蔽物方向からの脅威
	FVector m_lastCoverDirectionFromThreat;
	//三秒間の待機を開始するために記録する遮蔽物到着時刻
	float m_coverArrivalTime;
	//遮蔽物待機継続時間
	float m_coverHoldDuration;
	//遮蔽物到達判定半径
	float m_coverAcceptanceRadius;
	//使用中遮蔽物かどうか
	bool m_usingCover;
	//遮蔽物移動要求済みかどうか
	bool m_coverMoveRequested;
	//遮蔽物の待機ではなく射撃位置への移動中か示す変数
	bool m_peeking = false;
	//遮蔽物を出て射撃へ集中する期限
	float m_fireUntil = -1.f;
};
