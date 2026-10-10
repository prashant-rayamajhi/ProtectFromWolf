#include "Weapons/EnemyMeleeWeapon.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/DamageType.h"
#include "Components/BoxComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Player/PlayerChara.h"
#include "Enemy/EnemyChara.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimationPoseData.h"
#include "BonePose.h"
#include "Animation/AttributesRuntime.h"

//コンストラクタによる初期化処理を行う関数
AMeleeWeapon::AMeleeWeapon() : m_damage(5.0f), m_hasHit(false)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	m_weaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	RootComponent = m_weaponMesh;
	m_weaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	m_collisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	m_collisionBox->SetupAttachment(m_weaponMesh);

	m_collisionBox->SetBoxExtent(FVector(20.f, 20.f, 70.f));

	m_collisionBox->BodyInstance.bUseCCD = true;

	m_collisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	m_collisionBox->SetCollisionObjectType(ECC_WorldDynamic);
	m_collisionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	m_collisionBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	m_collisionBox->OnComponentBeginOverlap.AddDynamic(this, &AMeleeWeapon::OnWeaponOverlap);
}

//ゲーム開始時の処理を行う関数
void AMeleeWeapon::BeginPlay()
{
	Super::BeginPlay();
	if (AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner())) { AddTickPrerequisiteComponent(enemy->GetMesh()); }
	DeactivateWeapon();
}

//画面のコマ落ちで刃が相手を飛び越えても移動区間を検査する関数
void AMeleeWeapon::Tick(float _deltaTime)
{
	Super::Tick(_deltaTime);
	if (m_swingActive) { SampleSwing(); SweepBlade(); }
}

//描画更新より先に通過した命中区間を調べ、終了ブレンド前に次の一振りを判断する関数
void AMeleeWeapon::UpdateSwing()
{
	if (m_swingActive) { SampleSwing(); }
}

//回転を細分化しながら刃の箱を移動させ、一振りの途中に通過した相手も検出する関数
void AMeleeWeapon::SweepBlade()
{
	if (!m_collisionBox || !GetWorld() || !m_swingActive || !GetActorEnableCollision()) { return; }
	const FTransform current = m_collisionBox->GetComponentTransform();
	SweepBladePath(m_lastBlade, current);
	m_lastBlade = current;
}

//刃の移動と回転を分割し、同じ相手への重複命中を除いて接触を調べる関数
void AMeleeWeapon::SweepBladePath(const FTransform &_from, const FTransform &_to)
{
	const float distance = FVector::Distance(_from.GetLocation(), _to.GetLocation());
	//瞬間移動は剣の軌道に含めず、移動先から判定を再開する
	if (distance > 600.f) { return; }
	const float angle = FMath::RadiansToDegrees(_from.GetRotation().AngularDistance(_to.GetRotation()));
	const int32 steps = FMath::Clamp(FMath::CeilToInt(FMath::Max(distance / 15.f, angle / 10.f)), 1, 48);
	FCollisionQueryParams query(SCENE_QUERY_STAT(EnemyBladeSweep), false, this);
	query.AddIgnoredActor(GetOwner());
	FCollisionObjectQueryParams objects(ECC_Pawn);
	const FCollisionShape shape = FCollisionShape::MakeBox(m_collisionBox->GetScaledBoxExtent());
	FVector from = _from.GetLocation();
	for (int32 step = 1; step <= steps; ++step)
	{
		const float alpha = float(step) / steps;
		const FVector to = FMath::Lerp(_from.GetLocation(), _to.GetLocation(), alpha);
		const FQuat rotation = FQuat::Slerp(_from.GetRotation(), _to.GetRotation(), alpha);
		TArray<FHitResult> hits;
		GetWorld()->SweepMultiByObjectType(hits, from, to, rotation, objects, shape, query);
		for (const FHitResult &hit : hits) { ApplyBladeHit(hit.GetActor()); }
		from = to;
	}
}

//セクション内の命中区間を固定し、次の振りへ前回の軌道を持ち越さない関数
void AMeleeWeapon::SetSwingAnimation(UAnimMontage *_montage, float _sectionStart, float _sectionEnd, float _startRatio, float _endRatio)
{
	m_swingMontage = _montage;
	m_sectionStart = _sectionStart;
	const float duration = FMath::Max(0.f, _sectionEnd - _sectionStart);
	m_hitStart = _sectionStart + duration * FMath::Clamp(_startRatio, 0.f, 1.f);
	m_hitEnd = _sectionStart + duration * FMath::Clamp(_endRatio, _startRatio, 1.f);
	m_samplePosition = m_hitStart;
	m_swingStart = GetWorld()->GetTimeSeconds();
	if (AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner()))
	{
		if (UAnimInstance *animation = enemy->GetMesh()->GetAnimInstance())
		{
			m_swingStart -= FMath::Max(0.f, animation->Montage_GetPosition(_montage) - _sectionStart);
		}
	}
}

//圧縮済みアニメーションから手までの親ボーンを合成し、ソケットと剣の配置を再現する関数
bool AMeleeWeapon::GetAnimatedBlade(float _position, FTransform &_blade) const
{
	const AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner());
	UAnimMontage *montage = m_swingMontage.Get();
	if (!enemy || !montage || montage->SlotAnimTracks.IsEmpty() || !GetRootComponent()) { return false; }
	const USkeletalMeshComponent *mesh = enemy->GetMesh();
	if (!mesh || !mesh->GetAnimInstance() || GetRootComponent()->GetAttachParent() != mesh) { return false; }
	const FBoneContainer &bones = mesh->GetAnimInstance()->GetRequiredBones();
	const FAnimSegment *segment = montage->SlotAnimTracks[0].AnimTrack.GetSegmentAtTime(_position);
	if (!segment) { return false; }
	float time = 0.f;
	const UAnimSequence *sequence = Cast<UAnimSequence>(segment->GetAnimationData(_position, time));
	if (!sequence || !sequence->GetSkeleton()) { return false; }
	const FName socketName = GetRootComponent()->GetAttachSocketName();
	const USkeletalMeshSocket *socket = mesh->GetSocketByName(socketName);
	const FReferenceSkeleton &skeleton = sequence->GetSkeleton()->GetReferenceSkeleton();
	int32 bone = skeleton.FindBoneIndex(socket ? socket->BoneName : socketName);
	if (bone == INDEX_NONE) { return false; }
	//描画と同じ骨格補正とルート固定を適用した姿勢を取得する
	FMemMark mark(FMemStack::Get());
	FCompactPose pose;
	pose.SetBoneContainer(&bones);
	FBlendedCurve curve;
	curve.InitFrom(bones);
	UE::Anim::FStackAttributeContainer attributes;
	FAnimationPoseData data(pose, curve, attributes);
	const bool extractRoot = sequence->HasRootMotion() && mesh->GetAnimInstance()->RootMotionMode != ERootMotionMode::NoRootMotionExtraction;
	sequence->GetAnimationPose(data, FAnimExtractContext(double(time), extractRoot));
	FTransform hand = FTransform::Identity;
	while (bone != INDEX_NONE)
	{
		const FCompactPoseBoneIndex poseBone = bones.GetCompactPoseIndexFromSkeletonPoseIndex(FSkeletonPoseBoneIndex(bone));
		if (poseBone.GetInt() == INDEX_NONE) { return false; }
		hand = hand * pose[poseBone];
		bone = skeleton.GetParentIndex(bone);
	}
	const FTransform offset = m_collisionBox->GetComponentTransform().GetRelativeTransform(GetActorTransform());
	const FTransform socketLocal = socket ? socket->GetSocketLocalTransform() : FTransform::Identity;
	_blade = offset * GetRootComponent()->GetRelativeTransform() * socketLocal * hand * mesh->GetComponentTransform();
	return true;
}

//一秒を百二十分割した軌道で、フレーム落ちに隠れた振り抜きを補う関数
void AMeleeWeapon::SampleSwing(bool _finishSwing)
{
	if (!m_swingMontage.IsValid() || !m_swingActive || !GetActorEnableCollision() || !GetWorld()) { return; }
	const float position = _finishSwing ? m_hitEnd : FMath::Min(m_hitEnd, m_sectionStart + float(GetWorld()->GetTimeSeconds() - m_swingStart));
	if (position <= m_samplePosition) { return; }
	FTransform from;
	if (!GetAnimatedBlade(m_samplePosition, from)) { return; }
	while (m_samplePosition < position)
	{
		const float next = FMath::Min(position, m_samplePosition + 1.f / 120.f);
		FTransform to;
		if (!GetAnimatedBlade(next, to)) { break; }
		SweepBladePath(from, to);
		from = to;
		m_samplePosition = next;
	}
}

//攻撃モーションに合わせて当たり判定を有効にする処理を行う関数
void AMeleeWeapon::ActivateWeapon()
{
	if (!m_collisionBox) { return; }
	//タイマーと終了通知が同じ振りを再開して二重に命中させない
	if (m_swingMontage.IsValid() && m_samplePosition >= m_hitEnd) { return; }
	//同じ一振りの通知とタイマーが重なっても命中履歴を消さない
	if (m_collisionBox && m_collisionBox->GetCollisionEnabled() != ECollisionEnabled::NoCollision) { return; }
	m_hitActors.Empty();
	m_hasHit = false;
	m_lastBlade = m_collisionBox->GetComponentTransform();
	m_swingActive = true;

	if (m_collisionBox) { m_collisionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly); }
}

//攻撃モーション終了に合わせて当たり判定を無効にする処理を行う関数
void AMeleeWeapon::DeactivateWeapon(bool _finishSwing)
{
	if (_finishSwing) { SampleSwing(true); }
	m_swingActive = false;
	if (m_collisionBox) { m_collisionBox->SetCollisionEnabled(ECollisionEnabled::NoCollision); }

	m_hitActors.Empty();
	m_hasHit = false;
}

//プレイヤーへ一振りで与えるダメージ量を変更する関数
void AMeleeWeapon::SetDamage(float _damage)
{
	m_damage = _damage;
}

//剣へ重なったプレイヤーを検証し、一振り一回だけダメージを適用する関数
void AMeleeWeapon::OnWeaponOverlap(UPrimitiveComponent *_overlappedComp, AActor *_otherActor, UPrimitiveComponent *_otherComp, int32 _otherBodyIndex,
								   bool _bFromSweep, const FHitResult &_sweepResult)
{
	ApplyBladeHit(_otherActor);
}

//剣の接触経路によらず同じ相手への二重ダメージを防ぐ関数
void AMeleeWeapon::ApplyBladeHit(AActor *_otherActor)
{
	if (!m_swingActive || !IsValid(_otherActor) || _otherActor == this || _otherActor == GetOwner()) { return; }
	if (AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner()); enemy && (!enemy->IsAttacking() || enemy->GetHealthRatio() <= 0.f || enemy->IsKnockedBack())) { return; }

	if (m_hitActors.Contains(_otherActor)) { return; }

	bool bIsPlayer = _otherActor->ActorHasTag("Player") || _otherActor->IsA(APlayerChara::StaticClass());

	//プレイヤーを取得できた場合だけプレイヤー向け処理を実行する
	if (bIsPlayer)
	{
		m_hasHit = true;
		m_hitActors.Add(_otherActor);

		const float applied = UGameplayStatics::ApplyDamage(_otherActor, m_damage, GetInstigatorController(), this, UDamageType::StaticClass());
		if (applied > 0.f)
		{
			if (AEnemyChara *enemy = Cast<AEnemyChara>(GetOwner())) { enemy->ConfirmMeleeHit(); }
		}
	}
}
