#include "Animation/LocomotionRateLibrary.h"
#if WITH_EDITOR
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/World.h"
#include "Components/PointLightComponent.h"
#endif

AActor *ULocomotionRateLibrary::SpawnReviewCamera(UObject *_world)
{
#if WITH_EDITOR
	if (_world && _world->GetWorld())
	{
		ASceneCapture2D *camera = _world->GetWorld()->SpawnActor<ASceneCapture2D>();
		//検証カメラに補助照明を付け、逆光でも脚の姿勢を読み取れるようにする
		UPointLightComponent *light = NewObject<UPointLightComponent>(camera);
		light->SetupAttachment(camera->GetRootComponent());
		light->SetIntensity(150000.f);
		light->SetAttenuationRadius(2000.f);
		light->SetCastShadows(false);
		light->RegisterComponent();
		return camera;
	}
#endif
	return nullptr;
}

//既存の攻撃姿勢を保持しながら、別グループの走行を左右の脚だけへ反映する関数
bool ULocomotionRateLibrary::AddRetreatLayer(UAnimBlueprint *_blueprint)
{
#if WITH_EDITOR
	if (!_blueprint || !_blueprint->TargetSkeleton) { return false; }
	USkeleton *skeleton = _blueprint->TargetSkeleton;
	if (skeleton->GetReferenceSkeleton().FindBoneIndex(TEXT("LeftUpLeg")) == INDEX_NONE ||
		skeleton->GetReferenceSkeleton().FindBoneIndex(TEXT("RightUpLeg")) == INDEX_NONE) { return false; }
	for (UEdGraph *graph : _blueprint->FunctionGraphs)
	{
		if (!graph || graph->GetFName() != TEXT("AnimGraph")) { continue; }
		UAnimGraphNode_Root *root = nullptr;
		for (UEdGraphNode *node : graph->Nodes)
		{
			if (UAnimGraphNode_Slot *slot = Cast<UAnimGraphNode_Slot>(node))
			{
				if (slot->Node.SlotName == TEXT("RetreatSlot")) { return true; }
			}
			if (UAnimGraphNode_Root *candidate = Cast<UAnimGraphNode_Root>(node)) { root = candidate; }
		}
		UEdGraphPin *input = root ? root->FindPin(TEXT("Result")) : nullptr;
		if (!input || input->LinkedTo.Num() != 1) { return false; }
		UEdGraphPin *source = input->LinkedTo[0];
		//元の最終姿勢は一度だけ評価し、脚合成と走行スロットで共有する
		FGraphNodeCreator<UAnimGraphNode_SaveCachedPose> saveCreator(*graph);
		auto *save = saveCreator.CreateNode();
		save->CacheName = TEXT("EnemyRetreatBase");
		saveCreator.Finalize();
		FGraphNodeCreator<UAnimGraphNode_UseCachedPose> useCreator(*graph);
		auto *use = useCreator.CreateNode();
		use->SaveCachedPoseNode = save;
		useCreator.Finalize();
		FGraphNodeCreator<UAnimGraphNode_UseCachedPose> secondCreator(*graph);
		auto *second = secondCreator.CreateNode();
		second->SaveCachedPoseNode = save;
		secondCreator.Finalize();
		FGraphNodeCreator<UAnimGraphNode_Slot> slotCreator(*graph);
		auto *slot = slotCreator.CreateNode();
		slot->Node.SlotName = TEXT("RetreatSlot");
		slotCreator.Finalize();
		FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> blendCreator(*graph);
		auto *blend = blendCreator.CreateNode();
		blend->Node.AddPose();
		blend->Node.LayerSetup[0].BranchFilters.Add(FBranchFilter(TEXT("LeftUpLeg"), 0));
		blend->Node.LayerSetup[0].BranchFilters.Add(FBranchFilter(TEXT("RightUpLeg"), 0));
		blend->Node.BlendWeights[0] = 1.f;
		blendCreator.Finalize();
		const UEdGraphSchema_K2 *schema = GetDefault<UEdGraphSchema_K2>();
		input->BreakAllPinLinks();
		const bool connected = schema->TryCreateConnection(source, save->FindPin(TEXT("Pose"))) &&
			schema->TryCreateConnection(use->FindPin(TEXT("Pose")), blend->FindPin(TEXT("BasePose"))) &&
			schema->TryCreateConnection(second->FindPin(TEXT("Pose")), slot->FindPin(TEXT("Source"))) &&
			schema->TryCreateConnection(slot->FindPin(TEXT("Pose")), blend->FindPin(TEXT("BlendPoses_0"))) &&
			schema->TryCreateConnection(blend->FindPin(TEXT("Pose")), input);
		if (!connected) { input->BreakAllPinLinks(); schema->TryCreateConnection(source, input); return false; }
		skeleton->SetSlotGroupName(TEXT("RetreatSlot"), TEXT("RetreatGroup"));
		skeleton->MarkPackageDirty();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(_blueprint);
		return true;
	}
#endif
	return false;
}
