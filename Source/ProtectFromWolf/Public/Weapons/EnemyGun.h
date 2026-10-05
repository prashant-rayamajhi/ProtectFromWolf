#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyGun.generated.h"

class AEnemyBullet;
class AEnemyChara;
class UNiagaraSystem;
class USoundBase;

UCLASS()
//敵のバースト射撃、照準、発射演出を管理する銃
class PROTECTFROMWOLF_API AEnemyGun : public AActor
{
	GENERATED_BODY()

  public:
	//敵の銃身と銃口とバースト弾薬の初期値を設定する関数
	AEnemyGun();

  protected:
	//ゲーム開始時に装備する敵と銃口エフェクトを取得する関数
	virtual void BeginPlay() override;

  public:
	//射撃射程を取得する関数
	float GetFireRange() const
	{
		return m_fireRange;
	}

	//敵のマガジンを最大弾数まで補充する関数
	void ReloadAmmo();

	//射撃を開始する関数
	void StartFire();

	//射撃を停止する関数
	void StopFire();

	//弾薬を消費する関数
	bool ConsumeAmmo();

	//出力する弾薬か判定する関数
	bool IsOutOfAmmo() const
	{
		return m_currentAmmo <= 0;
	}

	//プレイヤーの現在位置へ向けて敵弾を一発発射する関数
	void FireShot();

	//現在の弾薬を取得する関数
	int32 GetCurrentAmmo() const
	{
		return m_currentAmmo;
	}

	//合計弾薬を取得する関数
	int32 GetTotalAmmo() const
	{
		return m_totalAmmo;
	}

	//射撃が可能か判定する関数
	bool CanFire() const
	{
		return m_canFire && m_currentAmmo > 0;
	}

	//マガジン大きさを取得する関数
	int32 GetClipSize() const
	{
		return m_clipSize;
	}

  private:
	//射撃を初期状態へ戻す関数
	void ResetFire();

  protected:
	//敵の銃へ一度に装填できる最大弾薬数
	UPROPERTY(EditAnywhere, Category = "Stats")
	int32 m_clipSize;

	//敵の銃で現在マガジン内に残っている弾薬数
	UPROPERTY(VisibleAnywhere, Category = "Stats")
	int32 m_currentAmmo;

	//敵の銃がマガジン外に所持している予備弾薬数
	UPROPERTY(VisibleAnywhere, Category = "Stats")
	int32 m_totalAmmo;

	//バースト中に次の敵弾を発射するまでの時間間隔
	UPROPERTY(EditAnywhere, Category = "Stats")
	float m_fireRate;

	//敵弾の照準位置を決めるために使用する最大射程
	UPROPERTY(EditAnywhere, Category = "Stats")
	float m_fireRange;

	//敵が弾切れからマガジン補充を完了するまでの時間
	UPROPERTY(EditAnywhere, Category = "Stats")
	float m_reloadTime;

	//敵が一回の射撃行動で連続発射する最大弾数
	UPROPERTY(EditAnywhere, Category = "Stats", meta = (ClampMin = "1"))
	int32 m_maxBurstShots;

	//一回のBurst終了後から次のBurst開始までの待機時間
	UPROPERTY(EditAnywhere, Category = "Stats", meta = (ClampMin = "0.1"))
	float m_burstCooldown;

  private:
	//銃の配置基準となるルートコンポーネント
	UPROPERTY(VisibleAnywhere)
	USceneComponent *m_sceneComp;

	//銃の見た目を表現するメッシュコンポーネント
	UPROPERTY(VisibleAnywhere)
	USkeletalMeshComponent *m_meshComp;

	//弾が発射される位置を示すコンポーネント
	UPROPERTY(VisibleAnywhere)
	USceneComponent *m_muzzle;

	//発射する弾のクラス
	UPROPERTY(EditDefaultsOnly, Category = "Spawning")
	TSubclassOf<AEnemyBullet> m_bulletClass;

	//発射時のマズルフラッシュエフェクト
	UPROPERTY(EditDefaultsOnly, Category = "Effects")
	UParticleSystem *m_muzzleFlash;

	//発射時のナイアガラエフェクト
	UPROPERTY(EditDefaultsOnly, Category = "Effects|SciFi")
	UNiagaraSystem *m_enemyMuzzlePulseNiagara;

	//発射時の効果音
	UPROPERTY(EditDefaultsOnly, Category = "Effects")
	USoundBase *m_muzzleSound;

	UPROPERTY(EditDefaultsOnly, Category = "Effects")
	//遠距離ミニオンがバースト射撃する時に再生するパルス銃声音
	USoundBase *m_minionPulseSound;

	//敵のバースト射撃で次の一発を発射するタイマー
	FTimerHandle m_autoFireTimer;
	//バースト再使用待ち時間タイマーかどうか
	FTimerHandle m_burstCooldownTimer;

	//残弾と射撃間隔を確認して次の弾を発射できるか示す変数
	bool m_canFire;
	//一回の攻撃命令で連続発射する敵弾の数
	int32 m_shotsInBurst;
};
