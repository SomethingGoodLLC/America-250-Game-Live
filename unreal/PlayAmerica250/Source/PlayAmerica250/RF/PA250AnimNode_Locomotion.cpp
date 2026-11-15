#include "PA250AnimNode_Locomotion.h"

#include "Animation/AnimInstanceProxy.h"
#include "IObjectChooser.h"
#include "PoseSearch/PoseHistoryProvider.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDefines.h"
#include "PoseSearch/PoseSearchEvent.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "PoseSearch/PoseSearchResult.h"
#include "PoseSearch/PoseSearchSchema.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PA250AnimNode_Locomotion)

void FPA250AnimNode_Locomotion::ResetLocomotion()
{
	Reset();
	State.Reset();
	Searches = 0;
	Jumps = 0;
}

// The search is the engine's (AnimNode_MotionMatching.cpp, UpdateAssetPlayer), without the
// multi-character interactions, blend spaces, mirroring and events this game does not use.
void FPA250AnimNode_Locomotion::UpdateAssetPlayer(const FAnimationUpdateContext& Context)
{
	using namespace UE::PoseSearch;

	const IPoseHistory* PoseHistory = nullptr;
	if (const FPoseHistoryProvider* Provider = Context.GetMessage<FPoseHistoryProvider>())
	{
		PoseHistory = Provider->GetPoseHistoryPtr();
	}
	bHadHistory = PoseHistory != nullptr;

	if (PoseHistory && !Databases.IsEmpty())
	{
		const UObject* AnimContext = Context.AnimInstanceProxy->GetAnimInstanceObject();
		// The player may have advanced more or less than expected (variable frame time, play rate).
		State.SearchResult.SelectedAnim = GetAnimAsset();
		State.SearchResult.SelectedTime = GetAccumulatedTime();
		State.SearchResult.bIsMirrored = false;
		State.SearchResult.BlendParameters = FVector::ZeroVector;

		const float DeltaTime = Context.GetDeltaTime();
		FSearchContext SearchContext(0.f, FFloatInterval(0.f, 0.f), FPoseSearchEvent());
		FChooserEvaluationContext ChooserContext(const_cast<UObject*>(AnimContext));
		SearchContext.AddRole(DefaultRole, &ChooserContext, PoseHistory);

		bool bSearch = true;
		FSearchResult SearchResult;
		if (State.ElapsedPoseSearchTime < SearchThrottleSeconds)
		{
			SearchResult.InitFrom(State.SearchResult);
			SearchContext.UpdateContinuingPoseSearchResult(SearchResult, SearchResult);
			bSearch = SearchContext.GetContinuingPoseSearchResult().PoseIdx == INDEX_NONE;
		}

		if (bSearch)
		{
			State.ElapsedPoseSearchTime = 0.f;
			TArray<const UObject*, TInlineAllocator<8>> Assets;
			for (const UPoseSearchDatabase* Database : Databases)
			{
				if (Database) Assets.Add(Database);
			}
			FSearchResults_Single Results;
			FPoseSearchContinuingProperties Continuing;
			Continuing.InitFrom(State.SearchResult, EPoseSearchInterruptMode::DoNotInterrupt);
PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS
			UPoseSearchLibrary::MotionMatch(SearchContext, TArrayView<const UObject*>(Assets), Continuing, Results);
PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS
			SearchResult = Results.GetBestResult();
			++Searches;
		}
		else
		{
			State.ElapsedPoseSearchTime += DeltaTime;
			SearchResult.bIsContinuingPoseSearch = true;
		}

		const float Wanted = CalculateWantedPlayRate(SearchResult, SearchContext, PlayRate, PoseHistory->GetTrajectorySpeedMultiplier(), FPoseSearchEvent());
		State.SearchResult.InitFrom(SearchResult, Wanted);
		if (const FPoseIndicesHistory* Indices = PoseHistory->GetPoseIndicesHistory())
		{
			// As the engine does: this thread owns the pose history.
			const_cast<FPoseIndicesHistory*>(Indices)->Update(SearchResult, DeltaTime, 0.3f);
		}

		const bool bJump = State.SearchResult.SelectedAnim && !State.SearchResult.bIsContinuingPoseSearch;
		if (bJump)
		{
			if (UAnimationAsset* Asset = State.SearchResult.GetAnimationAssetForRole())
			{
				BlendTo(Context, Asset, State.SearchResult.SelectedTime, State.SearchResult.bLoop, false, nullptr, BlendSeconds, nullptr,
					EAlphaBlendOption::Linear, false, NAME_None, FVector::ZeroVector, State.SearchResult.WantedPlayRate);
				++Jumps;
			}
		}
		UpdatePlayRate(State.SearchResult.WantedPlayRate);
	}

	FAnimNode_BlendStack_Standalone::UpdateAssetPlayer(Context);
}
