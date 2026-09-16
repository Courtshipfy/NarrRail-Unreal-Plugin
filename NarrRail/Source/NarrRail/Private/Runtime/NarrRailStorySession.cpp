#include "Runtime/NarrRailStorySession.h"

#include "Algo/Sort.h"
#include "Engine/World.h"

// 全局活跃会话列表定义
TArray<UNarrRailStorySession*> UNarrRailStorySession::ActiveSessions;

namespace NarrRailRuntime
{
// 当前受支持的会话快照版本。
//
// 恢复路径是一个「按版本分派」的开关，今天只有这一个分支。刻意不写成上限比较：把受支持版本
// 收在这里、由开关去分派，未来要加一条向前迁移时就是新增一个 case，而不是把恢复的结构改掉
// （FR-010）。也刻意不接受任何非 1 的值——更旧、更新、缺失（默认 0）、畸形（负数）一律拒绝。
constexpr int32 SupportedSessionSnapshotVersion = 1;

static bool TryParseBool(const FString& InValue, bool& OutValue)
{
    if (InValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || InValue == TEXT("1"))
    {
        OutValue = true;
        return true;
    }

    if (InValue.Equals(TEXT("false"), ESearchCase::IgnoreCase) || InValue == TEXT("0"))
    {
        OutValue = false;
        return true;
    }

    return false;
}

static bool TryParseInt(const FString& InValue, int32& OutValue)
{
    return LexTryParseString(OutValue, *InValue);
}

static bool TryParseFloat(const FString& InValue, float& OutValue)
{
    return LexTryParseString(OutValue, *InValue);
}
}

void UNarrRailStorySession::BeginDestroy()
{
    ActiveSessions.Remove(this);
    Super::BeginDestroy();
}


FNarrRailRuntimeResult UNarrRailStorySession::Initialize(const UNarrRailStoryAsset* InStoryAsset)
{
    const UNarrRailGlobalConfigAsset* LinkedGlobalConfig = nullptr;
    if (InStoryAsset != nullptr && !InStoryAsset->GlobalConfig.IsNull())
    {
        LinkedGlobalConfig = InStoryAsset->GlobalConfig.LoadSynchronous();
    }

    return InitializeWithGlobalConfig(InStoryAsset, LinkedGlobalConfig);
}

FNarrRailRuntimeResult UNarrRailStorySession::InitializeWithGlobalConfig(const UNarrRailStoryAsset* InStoryAsset, const UNarrRailGlobalConfigAsset* InGlobalConfig)
{
    if (InStoryAsset == nullptr)
    {
        SessionState = ENarrRailSessionState::Error;
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidInput, TEXT("Story asset is null."));
    }

    StoryAsset = InStoryAsset;
    GlobalConfigAsset = InGlobalConfig;
    SessionState = ENarrRailSessionState::Idle;
    StateBeforePause = ENarrRailSessionState::Idle;

    if (VariableContainer == nullptr)
    {
        VariableContainer = NewObject<UNarrRailVariableContainer>(this);
    }

    GlobalVariableContainer = nullptr;
    if (UWorld* World = GetWorld())
    {
        if (UGameInstance* GameInstance = World->GetGameInstance())
        {
            if (UNarrRailGlobalStateSubsystem* GlobalState = GameInstance->GetSubsystem<UNarrRailGlobalStateSubsystem>())
            {
                FString GlobalError;
                if (!GlobalState->ApplyGlobalConfig(GlobalConfigAsset, GlobalError))
                {
                    SessionState = ENarrRailSessionState::Error;
                    return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidInput, *GlobalError);
                }

                TArray<FNarrRailVariableDefinition> StoryGlobalDefinitions;
                for (FNarrRailVariableDefinition Definition : StoryAsset->Variables)
                {
                    if (Definition.bGlobalScope)
                    {
                        Definition.bGlobalScope = true;
                        StoryGlobalDefinitions.Add(Definition);
                    }
                }

                if (StoryGlobalDefinitions.Num() > 0)
                {
                    if (UNarrRailVariableContainer* SharedVariables = GlobalState->GetGlobalVariableContainer())
                    {
                        if (!SharedVariables->AddDefinitions(StoryGlobalDefinitions, true, GlobalError))
                        {
                            SessionState = ENarrRailSessionState::Error;
                            return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidInput, *GlobalError);
                        }
                    }
                }

                GlobalVariableContainer = GlobalState->GetGlobalVariableContainer();
            }
        }
    }

    ResetSessionContextFromAsset();

    if (!ActiveSessions.Contains(this))
    {
        ActiveSessions.Add(this);
    }

    return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Success, TEXT("Story session initialized."));
}

FNarrRailRuntimeResult UNarrRailStorySession::Start(const FName OverrideEntryNodeId)
{
    if (StoryAsset == nullptr)
    {
        SessionState = ENarrRailSessionState::Error;
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Session not initialized."));
    }

    const FName EntryNodeId = (OverrideEntryNodeId != NAME_None) ? OverrideEntryNodeId : StoryAsset->EntryNodeId;
    StateBeforePause = ENarrRailSessionState::Idle;
    ResetSessionContextFromAsset();
    CurrentMultiDialogueLineIndex = INDEX_NONE;
    ExhaustiveSelectedChoiceIndices.Reset();
    RuntimeVisibleChoiceIndexMap.Reset();
    ExhaustivePendingChoiceReturnStack.Reset();

    // 重置最后选择信息
    LastChoiceInfo = FNarrRailLastChoiceInfo();

    // 触发会话启动事件
    OnSessionStarted.Broadcast(EntryNodeId);

    return AdvanceToNode(EntryNodeId);
}

FNarrRailRuntimeResult UNarrRailStorySession::Next()
{
    if (StoryAsset == nullptr)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Session not initialized."));
    }

    if (SessionState == ENarrRailSessionState::Paused)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Session is paused. Call Resume() first."), Context.CurrentNodeId);
    }

    if (SessionState == ENarrRailSessionState::Completed)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Completed, TEXT("Session already completed."), Context.CurrentNodeId);
    }

    if (SessionState == ENarrRailSessionState::WaitingForChoice)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Current node requires Choose()."), Context.CurrentNodeId);
    }

    const FNarrRailNode* Node = FindNode(Context.CurrentNodeId);
    if (Node == nullptr)
    {
        SessionState = ENarrRailSessionState::Error;
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::MissingNode, TEXT("Current node not found."), Context.CurrentNodeId);
    }

    if (Node->NodeType == ENarrRailNodeType::End)
    {
        FName ReturnNodeId;
        if (TryPopExhaustiveReturn(ReturnNodeId))
        {
            return AdvanceToNode(ReturnNodeId);
        }

        SessionState = ENarrRailSessionState::Completed;
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Completed, TEXT("Reached end node."), Context.CurrentNodeId);
    }

    if (Node->NodeType == ENarrRailNodeType::Choice)
    {
        SessionState = ENarrRailSessionState::WaitingForChoice;
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Current node requires Choose()."), Context.CurrentNodeId);
    }

    if (Node->NodeType == ENarrRailNodeType::MultiDialogue)
    {
        const int32 TotalLines = Node->MultiDialogue.Lines.Num();
        if (TotalLines > 0 && CurrentMultiDialogueLineIndex + 1 < TotalLines)
        {
            CurrentMultiDialogueLineIndex += 1;

            UObject* PresenterObject = DialoguePresenter.GetObject();
            const bool bPresenterValid =
                PresenterObject != nullptr &&
                PresenterObject->GetClass()->ImplementsInterface(UNarrRailDialoguePresenterInterface::StaticClass());

            FNarrRailDialogueRequest Request;
            if (BuildMultiDialogueDisplay(*Node, Request))
            {
                if (bPresenterValid)
                {
                    INarrRailDialoguePresenterInterface::Execute_ShowDialogue(PresenterObject, Request);
                }

                FNarrRailNode DisplayNode = *Node;
                DisplayNode.NodeType = ENarrRailNodeType::Dialogue;
                DisplayNode.Dialogue.SpeakerId = Request.SpeakerId;
                DisplayNode.Dialogue.TextKey = Request.TextContent;
                DisplayNode.Dialogue.SpeechRate = Request.SpeechRate;
                OnNodeEntered.Broadcast(Context.CurrentNodeId, DisplayNode);

                SessionState = ENarrRailSessionState::Running;
                return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Success, TEXT("Advanced to next multi-dialogue line."), Context.CurrentNodeId);
            }
        }
    }

    FString ActionError;
    if (!ExecuteActions(Node->ExitActions, ActionError))
    {
        SessionState = ENarrRailSessionState::Error;
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidInput, *ActionError, Context.CurrentNodeId);
    }

    // 触发节点退出事件
    OnNodeExited.Broadcast(Context.CurrentNodeId, *Node);

    if (Node->NodeType == ENarrRailNodeType::Jump)
    {
        if (Node->JumpTargetNodeId == NAME_None)
        {
            SessionState = ENarrRailSessionState::Error;
            return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidInput, TEXT("Jump node has empty target."), Context.CurrentNodeId);
        }

        return AdvanceToNode(Node->JumpTargetNodeId);
    }

    FName NextNodeId = NAME_None;
    const FNarrRailRuntimeResult ResolveResult = ResolveNextByEdge(*Node, NextNodeId);
    if (ResolveResult.Code != ENarrRailRuntimeResultCode::Success)
    {
        if (ResolveResult.Code == ENarrRailRuntimeResultCode::Completed)
        {
            FName ReturnNodeId;
            if (TryPopExhaustiveReturn(ReturnNodeId))
            {
                return AdvanceToNode(ReturnNodeId);
            }

            SessionState = ENarrRailSessionState::Completed;
        }
        return ResolveResult;
    }

    return AdvanceToNode(NextNodeId);
}

FNarrRailRuntimeResult UNarrRailStorySession::Choose(const int32 ChoiceIndex)
{
    if (StoryAsset == nullptr)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Session not initialized."));
    }

    if (SessionState == ENarrRailSessionState::Paused)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Session is paused. Call Resume() first."), Context.CurrentNodeId);
    }

    const FNarrRailNode* Node = FindNode(Context.CurrentNodeId);
    if (Node == nullptr)
    {
        SessionState = ENarrRailSessionState::Error;
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::MissingNode, TEXT("Current node not found."), Context.CurrentNodeId);
    }

    if (Node->NodeType != ENarrRailNodeType::Choice)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Current node is not a choice node."), Context.CurrentNodeId);
    }

    const TArray<int32>* VisibleIndexMap = RuntimeVisibleChoiceIndexMap.Find(Context.CurrentNodeId);
    const TArray<int32> AvailableIndices = BuildAvailableChoiceIndices(*Node);

    int32 ActualChoiceIndex = INDEX_NONE;

    if (VisibleIndexMap != nullptr)
    {
        // VisibleIndexMap 映射: UI 可见位置(0-based) → 原始Choices索引
        // ChoiceIndex 必须是 0-based 进入可见列表，不再做模糊的 1-based 猜测
        const int32 NumVisible = VisibleIndexMap->Num();

        if (ChoiceIndex >= 0 && ChoiceIndex < NumVisible)
        {
            ActualChoiceIndex = (*VisibleIndexMap)[ChoiceIndex];
        }
        else
        {
            return FNarrRailRuntimeResult::Make(
                ENarrRailRuntimeResultCode::InvalidInput,
                *FString::Printf(TEXT("Choice index %d out of visible range [0, %d)."), ChoiceIndex, NumVisible),
                Context.CurrentNodeId);
        }
    }
    else
    {
        // 无 VisibleIndexMap 时退化为原始索引，兼容 0/1-based
        if (Node->Choices.IsValidIndex(ChoiceIndex))
        {
            ActualChoiceIndex = ChoiceIndex;
        }
        else if (ChoiceIndex > 0 && Node->Choices.IsValidIndex(ChoiceIndex - 1))
        {
            // 兼容 1-based UI 传参（仅限无 VisibleIndexMap 的回退路径）
            ActualChoiceIndex = ChoiceIndex - 1;
        }
        else
        {
            return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidInput, TEXT("Choice index out of range."), Context.CurrentNodeId);
        }
    }

    if (!AvailableIndices.Contains(ActualChoiceIndex))
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidInput, TEXT("Choice is not available."), Context.CurrentNodeId);
    }

    const FNarrRailChoiceOption& Option = Node->Choices[ActualChoiceIndex];
    if (Option.TargetNodeId == NAME_None)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidInput, TEXT("Choice target node is empty."), Context.CurrentNodeId);
    }

    // 更新最后一次选择的信息
    LastChoiceInfo.ChoiceNodeId = Context.CurrentNodeId;
    LastChoiceInfo.ChoiceIndex = ActualChoiceIndex;
    LastChoiceInfo.TargetNodeId = Option.TargetNodeId;
    LastChoiceInfo.ChoiceTextKey = Option.TextKey;
    LastChoiceInfo.bValid = true;

    // 触发选择事件
    OnChoiceSelected.Broadcast(Context.CurrentNodeId, ActualChoiceIndex, Option.TargetNodeId);

    if (Node->ChoiceMode == ENarrRailChoiceMode::ExhaustiveUntilComplete)
    {
        TSet<int32>& Selected = ExhaustiveSelectedChoiceIndices.FindOrAdd(Context.CurrentNodeId);
        Selected.Add(ActualChoiceIndex);
        ExhaustivePendingChoiceReturnStack.Add(Context.CurrentNodeId);
    }

    FString ActionError;
    if (!ExecuteActions(Node->ExitActions, ActionError))
    {
        SessionState = ENarrRailSessionState::Error;
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidInput, *ActionError, Context.CurrentNodeId);
    }

    return AdvanceToNode(Option.TargetNodeId);
}

FNarrRailRuntimeResult UNarrRailStorySession::Pause()
{
    if (StoryAsset == nullptr)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Session not initialized."));
    }

    if (SessionState == ENarrRailSessionState::Paused)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Session already paused."), Context.CurrentNodeId);
    }

    if (SessionState == ENarrRailSessionState::Completed || SessionState == ENarrRailSessionState::Error)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Cannot pause completed or error session."), Context.CurrentNodeId);
    }

    StateBeforePause = SessionState;
    SessionState = ENarrRailSessionState::Paused;
    return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Success, TEXT("Session paused."), Context.CurrentNodeId);
}

FNarrRailRuntimeResult UNarrRailStorySession::Resume()
{
    if (StoryAsset == nullptr)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Session not initialized."));
    }

    if (SessionState != ENarrRailSessionState::Paused)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Session is not paused."), Context.CurrentNodeId);
    }

    SessionState = StateBeforePause;
    return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Success, TEXT("Session resumed."), Context.CurrentNodeId);
}

FNarrRailRuntimeResult UNarrRailStorySession::Stop()
{
    if (StoryAsset == nullptr)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Session not initialized."));
    }

    const FNarrRailNode* Node = FindNode(Context.CurrentNodeId);
    if (Node != nullptr)
    {
        FString ActionError;
        if (!ExecuteActions(Node->ExitActions, ActionError))
        {
            SessionState = ENarrRailSessionState::Error;
            return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidInput, *ActionError, Context.CurrentNodeId);
        }
    }

    SessionState = ENarrRailSessionState::Completed;

    // 触发会话结束事件
    OnSessionEnded.Broadcast(Context.CurrentNodeId, SessionState);

    return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Completed, TEXT("Session stopped."), Context.CurrentNodeId);
}

int32 UNarrRailStorySession::GetCurrentMultiDialogueTotalLines() const
{
    const FNarrRailNode* Node = FindNode(Context.CurrentNodeId);
    if (Node == nullptr || Node->NodeType != ENarrRailNodeType::MultiDialogue)
    {
        return 0;
    }

    return Node->MultiDialogue.Lines.Num();
}

bool UNarrRailStorySession::IsCurrentNodeMultiDialogue() const
{
    const FNarrRailNode* Node = FindNode(Context.CurrentNodeId);
    return Node != nullptr && Node->NodeType == ENarrRailNodeType::MultiDialogue;
}

bool UNarrRailStorySession::GetCurrentNode(FNarrRailNode& OutNode) const
{
    const FNarrRailNode* Node = FindNode(Context.CurrentNodeId);
    if (Node == nullptr)
    {
        return false;
    }

    OutNode = *Node;
    return true;
}

TArray<FNarrRailChoiceOption> UNarrRailStorySession::GetCurrentChoices() const
{
    const FNarrRailNode* Node = FindNode(Context.CurrentNodeId);
    if (Node == nullptr || Node->NodeType != ENarrRailNodeType::Choice)
    {
        return {};
    }

    return BuildVisibleChoiceOptions(*Node);
}

// 便捷的变量访问接口实现
FNarrRailVariableResult UNarrRailStorySession::GetVariableBool(FName VariableName, bool& OutValue) const
{
    UNarrRailVariableContainer* Container = FindVariableContainerForName(VariableName);
    return Container != nullptr
        ? Container->GetBool(VariableName, OutValue)
        : FNarrRailVariableResult::MakeError(ENarrRailVariableError::VariableNotFound, TEXT("Variable container not initialized."));
}

FNarrRailVariableResult UNarrRailStorySession::GetVariableInt(FName VariableName, int32& OutValue) const
{
    UNarrRailVariableContainer* Container = FindVariableContainerForName(VariableName);
    return Container != nullptr
        ? Container->GetInt(VariableName, OutValue)
        : FNarrRailVariableResult::MakeError(ENarrRailVariableError::VariableNotFound, TEXT("Variable container not initialized."));
}

FNarrRailVariableResult UNarrRailStorySession::GetVariableFloat(FName VariableName, float& OutValue) const
{
    UNarrRailVariableContainer* Container = FindVariableContainerForName(VariableName);
    return Container != nullptr
        ? Container->GetFloat(VariableName, OutValue)
        : FNarrRailVariableResult::MakeError(ENarrRailVariableError::VariableNotFound, TEXT("Variable container not initialized."));
}

FNarrRailVariableResult UNarrRailStorySession::GetVariableString(FName VariableName, FString& OutValue) const
{
    UNarrRailVariableContainer* Container = FindVariableContainerForName(VariableName);
    return Container != nullptr
        ? Container->GetString(VariableName, OutValue)
        : FNarrRailVariableResult::MakeError(ENarrRailVariableError::VariableNotFound, TEXT("Variable container not initialized."));
}

FNarrRailVariableResult UNarrRailStorySession::SetVariableBool(FName VariableName, bool Value)
{
    UNarrRailVariableContainer* Container = FindVariableContainerForName(VariableName);
    FNarrRailVariableResult Result = Container != nullptr
        ? Container->SetBool(VariableName, Value)
        : FNarrRailVariableResult::MakeError(ENarrRailVariableError::VariableNotFound, TEXT("Variable container not initialized."));
    SyncVariableSnapshotToContext();
    return Result;
}

FNarrRailVariableResult UNarrRailStorySession::SetVariableInt(FName VariableName, int32 Value)
{
    UNarrRailVariableContainer* Container = FindVariableContainerForName(VariableName);
    FNarrRailVariableResult Result = Container != nullptr
        ? Container->SetInt(VariableName, Value)
        : FNarrRailVariableResult::MakeError(ENarrRailVariableError::VariableNotFound, TEXT("Variable container not initialized."));
    SyncVariableSnapshotToContext();
    return Result;
}

FNarrRailVariableResult UNarrRailStorySession::SetVariableFloat(FName VariableName, float Value)
{
    UNarrRailVariableContainer* Container = FindVariableContainerForName(VariableName);
    FNarrRailVariableResult Result = Container != nullptr
        ? Container->SetFloat(VariableName, Value)
        : FNarrRailVariableResult::MakeError(ENarrRailVariableError::VariableNotFound, TEXT("Variable container not initialized."));
    SyncVariableSnapshotToContext();
    return Result;
}

FNarrRailVariableResult UNarrRailStorySession::SetVariableString(FName VariableName, const FString& Value)
{
    UNarrRailVariableContainer* Container = FindVariableContainerForName(VariableName);
    FNarrRailVariableResult Result = Container != nullptr
        ? Container->SetString(VariableName, Value)
        : FNarrRailVariableResult::MakeError(ENarrRailVariableError::VariableNotFound, TEXT("Variable container not initialized."));
    SyncVariableSnapshotToContext();
    return Result;
}

bool UNarrRailStorySession::GetPresetSpeaker(FName SpeakerId, FNarrRailPresetSpeaker& OutSpeaker) const
{
    if (UWorld* World = GetWorld())
    {
        if (UGameInstance* GameInstance = World->GetGameInstance())
        {
            if (const UNarrRailGlobalStateSubsystem* GlobalState = GameInstance->GetSubsystem<UNarrRailGlobalStateSubsystem>())
            {
                return GlobalState->GetPresetSpeaker(SpeakerId, OutSpeaker);
            }
        }
    }

    return false;
}

FString UNarrRailStorySession::ResolveSpeakerDisplayName(FName SpeakerId) const
{
    FNarrRailPresetSpeaker Speaker;
    if (GetPresetSpeaker(SpeakerId, Speaker) && !Speaker.DisplayName.IsEmpty())
    {
        return Speaker.DisplayName;
    }

    return SpeakerId.ToString();
}

// ============================================================================
// 存档快照：采集
// ============================================================================

FNarrRailStorySessionSnapshot UNarrRailStorySession::GetSessionSnapshot() const
{
    FNarrRailStorySessionSnapshot Snapshot;

    // 显式盖章，而不是依赖字段默认值。字段默认值是 0，那是「缺失/畸形」的哨兵，采集要是产出
    // 了它，自己采的快照就会被自己的版本门拒掉。
    Snapshot.SnapshotVersion = NarrRailRuntime::SupportedSessionSnapshotVersion;

    Snapshot.StoryId = StoryAsset != nullptr ? StoryAsset->StoryId : NAME_None;
    Snapshot.StoryAssetPath = StoryAsset != nullptr ? FSoftObjectPath(StoryAsset) : FSoftObjectPath();
    Snapshot.GlobalConfigPath = GlobalConfigAsset != nullptr ? FSoftObjectPath(GlobalConfigAsset) : FSoftObjectPath();
    Snapshot.SessionState = SessionState;
    Snapshot.StateBeforePause = StateBeforePause;
    Snapshot.CurrentNodeId = Context.CurrentNodeId;
    Snapshot.NodeHistory = Context.NodeHistory;
    Snapshot.EmittedEvents = Context.EmittedEvents;
    Snapshot.CurrentMultiDialogueLineIndex = CurrentMultiDialogueLineIndex;
    Snapshot.LastChoiceInfo = LastChoiceInfo;
    Snapshot.ExhaustivePendingChoiceReturnStack = ExhaustivePendingChoiceReturnStack;

    if (VariableContainer != nullptr)
    {
        Snapshot.LocalVariableSnapshot = VariableContainer->GetSnapshot();
    }

    // ExhaustiveSelectedChoiceIndices 是 TSet，没有稳定的迭代顺序，直接落盘会让「同一状态」
    // 的两次采集产出不相等的快照。这里转成每个节点一条的记录，并把索引排序，让快照可比较——
    // 「采集不改变状态」的断言（FR-006 / SC-002）正是靠这一点成立的。
    for (const TPair<FName, TSet<int32>>& Pair : ExhaustiveSelectedChoiceIndices)
    {
        FNarrRailChoiceSelectionSnapshot& ChoiceSnapshot = Snapshot.ExhaustiveChoiceSelections.AddDefaulted_GetRef();
        ChoiceSnapshot.ChoiceNodeId = Pair.Key;
        ChoiceSnapshot.SelectedChoiceIndices = Pair.Value.Array();
        ChoiceSnapshot.SelectedChoiceIndices.Sort();
    }

    return Snapshot;
}

// ============================================================================
// 存档快照：三道门
//
// 三个函数都只读不写，因此调用方可以把它们全部跑完再决定是否提交。
// ============================================================================

FNarrRailRuntimeResult UNarrRailStorySession::ValidateSnapshotVersion(const FNarrRailStorySessionSnapshot& Snapshot) const
{
    // 按版本分派，而不是上限比较。今天恰好只有一个受支持分支；要加一条向前迁移时，这里新增
    // 一个 case 把旧布局升到当前布局，恢复路径本身的结构不变（FR-010）。
    switch (Snapshot.SnapshotVersion)
    {
    case NarrRailRuntime::SupportedSessionSnapshotVersion:
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Success, TEXT("Snapshot version is supported."));

    default:
        // 一个 default 覆盖四种情况：更旧、更新、缺失（反序列化后仍是默认的 0）、畸形（负数）。
        // FR-010 对它们的要求一致——显式失败、不改状态——所以不需要再分情形。
        return FNarrRailRuntimeResult::Make(
            ENarrRailRuntimeResultCode::InvalidInput,
            *FString::Printf(
                TEXT("Unsupported NarrRail session snapshot version %d (supported: %d)."),
                Snapshot.SnapshotVersion,
                NarrRailRuntime::SupportedSessionSnapshotVersion));
    }
}

FNarrRailRuntimeResult UNarrRailStorySession::ValidateSnapshotIdentity(const FNarrRailStorySessionSnapshot& Snapshot) const
{
    if (StoryAsset == nullptr)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Session not initialized."));
    }

    if (!Snapshot.StoryAssetPath.IsNull() && FSoftObjectPath(StoryAsset) != Snapshot.StoryAssetPath)
    {
        return FNarrRailRuntimeResult::Make(
            ENarrRailRuntimeResultCode::InvalidInput,
            *FString::Printf(TEXT("Snapshot StoryAssetPath '%s' does not match current StoryAssetPath '%s'."),
                *Snapshot.StoryAssetPath.ToString(),
                *FSoftObjectPath(StoryAsset).ToString()));
    }

    if (!Snapshot.GlobalConfigPath.IsNull() && FSoftObjectPath(GlobalConfigAsset) != Snapshot.GlobalConfigPath)
    {
        return FNarrRailRuntimeResult::Make(
            ENarrRailRuntimeResultCode::InvalidInput,
            *FString::Printf(TEXT("Snapshot GlobalConfigPath '%s' does not match current GlobalConfigPath '%s'."),
                *Snapshot.GlobalConfigPath.ToString(),
                *FSoftObjectPath(GlobalConfigAsset).ToString()));
    }

    if (Snapshot.StoryId != NAME_None && StoryAsset->StoryId != NAME_None && Snapshot.StoryId != StoryAsset->StoryId)
    {
        return FNarrRailRuntimeResult::Make(
            ENarrRailRuntimeResultCode::InvalidInput,
            *FString::Printf(TEXT("Snapshot StoryId '%s' does not match current StoryId '%s'."),
                *Snapshot.StoryId.ToString(),
                *StoryAsset->StoryId.ToString()));
    }

    return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Success, TEXT("Snapshot identity checks passed."));
}

FNarrRailRuntimeResult UNarrRailStorySession::ValidateSnapshotConsistency(const FNarrRailStorySessionSnapshot& Snapshot) const
{
    // 只校验「恢复之后运行时真的会去解引用」的节点 Id。
    //
    // 只被记录、不被解引用的 Id 不在门内。NodeHistory 和 LastChoiceInfo 都只是原样交给调用方
    // （GetHistory / GetLastChoice），运行时不解析它们；而且 NodeHistory 只增不减，作者删掉一个
    // 曾访问过的节点之后它必然留下一个悬空 Id。对它们设门等于在普通编辑之后就拒绝存档，恰恰是
    // FR-009 要避免的结果。规则与理由见 specs/0001-save-load-snapshots/data-model.md invariant 6。

    // 检查 1：当前节点必须能解析。
    const FNarrRailNode* CurrentNode = nullptr;
    if (Snapshot.CurrentNodeId != NAME_None)
    {
        CurrentNode = FindNode(Snapshot.CurrentNodeId);
        if (CurrentNode == nullptr)
        {
            return FNarrRailRuntimeResult::Make(
                ENarrRailRuntimeResultCode::MissingNode,
                *FString::Printf(TEXT("Snapshot current node '%s' does not exist in the loaded story asset."), *Snapshot.CurrentNodeId.ToString()),
                Snapshot.CurrentNodeId);
        }
    }

    // 检查 2：多行对话行索引要么是哨兵，要么落在当前节点的行数范围内。
    if (Snapshot.CurrentMultiDialogueLineIndex != INDEX_NONE)
    {
        if (CurrentNode == nullptr || CurrentNode->NodeType != ENarrRailNodeType::MultiDialogue)
        {
            return FNarrRailRuntimeResult::Make(
                ENarrRailRuntimeResultCode::InvalidInput,
                *FString::Printf(
                    TEXT("Snapshot CurrentMultiDialogueLineIndex is %d, but node '%s' is not a MultiDialogue node."),
                    Snapshot.CurrentMultiDialogueLineIndex,
                    *Snapshot.CurrentNodeId.ToString()),
                Snapshot.CurrentNodeId);
        }

        const int32 TotalLines = CurrentNode->MultiDialogue.Lines.Num();
        if (Snapshot.CurrentMultiDialogueLineIndex < 0 || Snapshot.CurrentMultiDialogueLineIndex >= TotalLines)
        {
            return FNarrRailRuntimeResult::Make(
                ENarrRailRuntimeResultCode::InvalidInput,
                *FString::Printf(
                    TEXT("Snapshot CurrentMultiDialogueLineIndex %d is outside node '%s' line range [0, %d)."),
                    Snapshot.CurrentMultiDialogueLineIndex,
                    *Snapshot.CurrentNodeId.ToString(),
                    TotalLines),
                Snapshot.CurrentNodeId);
        }
    }

    // 检查 3：被消费的选项记录所指向的节点必须仍能解析，且每个已消费的选项索引必须落在该节点
    // 的选项数范围内。
    for (const FNarrRailChoiceSelectionSnapshot& ChoiceSnapshot : Snapshot.ExhaustiveChoiceSelections)
    {
        if (ChoiceSnapshot.ChoiceNodeId == NAME_None)
        {
            // 与提交阶段保持一致：空记录被忽略，不当作失败。
            continue;
        }

        const FNarrRailNode* ChoiceNode = FindNode(ChoiceSnapshot.ChoiceNodeId);
        if (ChoiceNode == nullptr)
        {
            return FNarrRailRuntimeResult::Make(
                ENarrRailRuntimeResultCode::InvalidInput,
                *FString::Printf(
                    TEXT("Snapshot ExhaustiveChoiceSelections references node '%s', which does not exist in the loaded story asset."),
                    *ChoiceSnapshot.ChoiceNodeId.ToString()),
                ChoiceSnapshot.ChoiceNodeId);
        }

        if (ChoiceNode->NodeType != ENarrRailNodeType::Choice)
        {
            return FNarrRailRuntimeResult::Make(
                ENarrRailRuntimeResultCode::InvalidInput,
                *FString::Printf(
                    TEXT("Snapshot ExhaustiveChoiceSelections references node '%s', which is no longer a Choice node."),
                    *ChoiceSnapshot.ChoiceNodeId.ToString()),
                ChoiceSnapshot.ChoiceNodeId);
        }

        const int32 OptionCount = ChoiceNode->Choices.Num();
        for (const int32 ChoiceIndex : ChoiceSnapshot.SelectedChoiceIndices)
        {
            if (ChoiceIndex < 0 || ChoiceIndex >= OptionCount)
            {
                return FNarrRailRuntimeResult::Make(
                    ENarrRailRuntimeResultCode::InvalidInput,
                    *FString::Printf(
                        TEXT("Snapshot consumed choice index %d on node '%s' is outside the node's option range [0, %d)."),
                        ChoiceIndex,
                        *ChoiceSnapshot.ChoiceNodeId.ToString(),
                        OptionCount),
                    ChoiceSnapshot.ChoiceNodeId);
            }
        }
    }

    // 检查 4：穷举选择返回栈上的节点 Id 必须仍能解析。
    //
    // 这一条是 T001 枚举参考实现时补上的。返回栈会被 TryPopExhaustiveReturn 弹出并直接交给
    // AdvanceToNode，所以栈里一个已被删除的节点不会在恢复时暴露，而是等到该分支结束的那一刻
    // 才以 MissingNode 失败——那时上下文已经远离成因，恢复本身却报告了成功。在这里拦下来，
    // 失败就落在成因旁边。
    for (const FName ReturnNodeId : Snapshot.ExhaustivePendingChoiceReturnStack)
    {
        if (ReturnNodeId == NAME_None)
        {
            continue;
        }

        if (FindNode(ReturnNodeId) == nullptr)
        {
            return FNarrRailRuntimeResult::Make(
                ENarrRailRuntimeResultCode::MissingNode,
                *FString::Printf(
                    TEXT("Snapshot ExhaustivePendingChoiceReturnStack references node '%s', which does not exist in the loaded story asset."),
                    *ReturnNodeId.ToString()),
                ReturnNodeId);
        }
    }

    return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Success, TEXT("Snapshot consistency checks passed."));
}

// ============================================================================
// 存档快照：恢复
// ============================================================================

FNarrRailRuntimeResult UNarrRailStorySession::RestoreSessionSnapshot(const FNarrRailStorySessionSnapshot& Snapshot, const bool bRefreshPresenter)
{
    // 三道门全部跑完并且都通过，才允许第一次写入。任何一次提前返回都留下一个逐字段未被触碰的
    // 会话（FR-008）。
    //
    // 顺序是刻意的，不是随手排的：版本门最便宜、也最可能在「存档来自别处」时先失败；身份校验
    // 负责在版本正确但对象不对时给出比一致性门更准确的报错；一致性门最后跑，因为它要做节点解析，
    // 是三者中最贵的一个。
    const FNarrRailRuntimeResult VersionResult = ValidateSnapshotVersion(Snapshot);
    if (VersionResult.Code != ENarrRailRuntimeResultCode::Success)
    {
        return VersionResult;
    }

    const FNarrRailRuntimeResult IdentityResult = ValidateSnapshotIdentity(Snapshot);
    if (IdentityResult.Code != ENarrRailRuntimeResultCode::Success)
    {
        return IdentityResult;
    }

    const FNarrRailRuntimeResult ConsistencyResult = ValidateSnapshotConsistency(Snapshot);
    if (ConsistencyResult.Code != ENarrRailRuntimeResultCode::Success)
    {
        return ConsistencyResult;
    }

    // === 提交：以下不再有失败路径 ===

    SessionState = Snapshot.SessionState;
    StateBeforePause = Snapshot.StateBeforePause;
    Context.CurrentNodeId = Snapshot.CurrentNodeId;
    Context.NodeHistory = Snapshot.NodeHistory;
    Context.EmittedEvents = Snapshot.EmittedEvents;
    CurrentMultiDialogueLineIndex = Snapshot.CurrentMultiDialogueLineIndex;
    LastChoiceInfo = Snapshot.LastChoiceInfo;
    ExhaustivePendingChoiceReturnStack = Snapshot.ExhaustivePendingChoiceReturnStack;
    RuntimeVisibleChoiceIndexMap.Reset();

    // 重建内存中的 TSet 缓存。它本身不落盘，只以上面 ExhaustiveChoiceSelections 的形式存在。
    ExhaustiveSelectedChoiceIndices.Reset();
    for (const FNarrRailChoiceSelectionSnapshot& ChoiceSnapshot : Snapshot.ExhaustiveChoiceSelections)
    {
        if (ChoiceSnapshot.ChoiceNodeId == NAME_None)
        {
            continue;
        }

        TSet<int32>& Selected = ExhaustiveSelectedChoiceIndices.FindOrAdd(ChoiceSnapshot.ChoiceNodeId);
        for (const int32 ChoiceIndex : ChoiceSnapshot.SelectedChoiceIndices)
        {
            if (ChoiceIndex >= 0)
            {
                Selected.Add(ChoiceIndex);
            }
        }
    }

    if (VariableContainer != nullptr)
    {
        VariableContainer->RestoreFromSnapshot(Snapshot.LocalVariableSnapshot);
    }
    SyncVariableSnapshotToContext();

    const FNarrRailNode* CurrentNode = FindNode(Context.CurrentNodeId);
    if (CurrentNode != nullptr && CurrentNode->NodeType == ENarrRailNodeType::Choice)
    {
        RuntimeVisibleChoiceIndexMap.Add(Context.CurrentNodeId, BuildVisibleChoiceIndices(*CurrentNode));
    }

    if (bRefreshPresenter)
    {
        RefreshCurrentNodeAfterRestore();
    }

    return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Success, TEXT("NarrRail session snapshot restored."), Context.CurrentNodeId);
}

void UNarrRailStorySession::RefreshCurrentNodeAfterRestore()
{
    const FNarrRailNode* Node = FindNode(Context.CurrentNodeId);
    if (Node == nullptr)
    {
        return;
    }

    UObject* PresenterObject = DialoguePresenter.GetObject();
    const bool bPresenterValid =
        PresenterObject != nullptr &&
        PresenterObject->GetClass()->ImplementsInterface(UNarrRailDialoguePresenterInterface::StaticClass());

    FNarrRailNode NodeForEvent = *Node;

    if (bPresenterValid)
    {
        if (Node->NodeType == ENarrRailNodeType::Dialogue)
        {
            FNarrRailDialogueRequest Request;
            Request.NodeId = Node->NodeId;
            Request.SpeakerId = Node->Dialogue.SpeakerId;
            Request.TextContent = Node->Dialogue.TextKey;
            Request.SpeechRate = Node->Dialogue.SpeechRate;
            Request.VoiceAsset = Node->Dialogue.VoiceAsset;
            Request.bAutoAdvance = false;
            INarrRailDialoguePresenterInterface::Execute_ShowDialogue(PresenterObject, Request);
        }
        else if (Node->NodeType == ENarrRailNodeType::MultiDialogue)
        {
            FNarrRailDialogueRequest Request;
            if (BuildMultiDialogueDisplay(*Node, Request))
            {
                INarrRailDialoguePresenterInterface::Execute_ShowDialogue(PresenterObject, Request);

                NodeForEvent.NodeType = ENarrRailNodeType::Dialogue;
                NodeForEvent.Dialogue.SpeakerId = Request.SpeakerId;
                NodeForEvent.Dialogue.TextKey = Request.TextContent;
                NodeForEvent.Dialogue.SpeechRate = Request.SpeechRate;
            }
        }
        else if (Node->NodeType == ENarrRailNodeType::Choice)
        {
            FNarrRailChoiceRequest Request;
            Request.NodeId = Node->NodeId;
            Request.Choices = BuildVisibleChoiceOptions(*Node);
            Request.Session = this;
            INarrRailDialoguePresenterInterface::Execute_ShowChoices(PresenterObject, Request);
        }
    }

    OnNodeEntered.Broadcast(Context.CurrentNodeId, NodeForEvent);

    if (Node->NodeType == ENarrRailNodeType::Choice)
    {
        OnChoicesReady.Broadcast(Context.CurrentNodeId, BuildVisibleChoiceOptions(*Node));
    }

    if (SessionState == ENarrRailSessionState::Completed)
    {
        OnSessionEnded.Broadcast(Context.CurrentNodeId, SessionState);
    }
}

void UNarrRailStorySession::RegisterDialoguePresenter(TScriptInterface<INarrRailDialoguePresenterInterface> Presenter)
{
    DialoguePresenter = Presenter;

    UObject* PresenterObject = DialoguePresenter.GetObject();
    const bool bImplementsInterface =
        PresenterObject != nullptr &&
        PresenterObject->GetClass()->ImplementsInterface(UNarrRailDialoguePresenterInterface::StaticClass());

    UE_LOG(LogTemp, Log, TEXT("[NarrRail] RegisterDialoguePresenter: Object=%s, InterfacePtrValid=%s, ImplementsInterface=%s"),
        PresenterObject ? *PresenterObject->GetName() : TEXT("None"),
        DialoguePresenter.GetInterface() ? TEXT("true") : TEXT("false"),
        bImplementsInterface ? TEXT("true") : TEXT("false"));
}

void UNarrRailStorySession::UnregisterDialoguePresenter()
{
    DialoguePresenter = nullptr;
}

void UNarrRailStorySession::ResetSessionContextFromAsset()
{
    Context = FNarrRailSessionContext{};
    CurrentMultiDialogueLineIndex = INDEX_NONE;
    RuntimeVisibleChoiceIndexMap.Reset();
    ExhaustivePendingChoiceReturnStack.Reset();

    if (StoryAsset == nullptr || VariableContainer == nullptr)
    {
        return;
    }

    TArray<FNarrRailVariableDefinition> LocalDefinitions;
    for (const FNarrRailVariableDefinition& Definition : StoryAsset->Variables)
    {
        if (!Definition.bGlobalScope)
        {
            LocalDefinitions.Add(Definition);
        }
    }

    VariableContainer->Initialize(LocalDefinitions);
    SyncVariableSnapshotToContext();
}

void UNarrRailStorySession::SyncVariableSnapshotToContext()
{
    Context.VariableSnapshot.Reset();

    if (GlobalVariableContainer != nullptr)
    {
        Context.VariableSnapshot.Append(GlobalVariableContainer->GetSnapshot());
    }

    if (VariableContainer != nullptr)
    {
        Context.VariableSnapshot.Append(VariableContainer->GetSnapshot());
    }
}

UNarrRailVariableContainer* UNarrRailStorySession::FindVariableContainerForName(const FName VariableName) const
{
    if (VariableContainer != nullptr && VariableContainer->HasVariable(VariableName))
    {
        return VariableContainer;
    }

    if (GlobalVariableContainer != nullptr && GlobalVariableContainer->HasVariable(VariableName))
    {
        return GlobalVariableContainer;
    }

    return VariableContainer;
}

bool UNarrRailStorySession::BuildMultiDialogueDisplay(const FNarrRailNode& Node, FNarrRailDialogueRequest& OutRequest) const
{
    const int32 TotalLines = Node.MultiDialogue.Lines.Num();
    if (TotalLines <= 0) return false;
    if (CurrentMultiDialogueLineIndex < 0 || CurrentMultiDialogueLineIndex >= TotalLines) return false;

    const FNarrRailDialogueLine& Line = Node.MultiDialogue.Lines[CurrentMultiDialogueLineIndex];

    OutRequest.NodeId = Node.NodeId;
    OutRequest.SpeakerId = Node.MultiDialogue.SpeakerId;
    OutRequest.TextContent = Line.TextKey;
    OutRequest.SpeechRate = 1.0f;
    OutRequest.VoiceAsset = nullptr;
    OutRequest.bAutoAdvance = false;
    return true;
}

bool UNarrRailStorySession::TryPopExhaustiveReturn(FName& OutReturnNodeId)
{
    if (ExhaustivePendingChoiceReturnStack.Num() > 0)
    {
        OutReturnNodeId = ExhaustivePendingChoiceReturnStack.Pop();
        return true;
    }

    OutReturnNodeId = NAME_None;
    return false;
}

FNarrRailRuntimeResult UNarrRailStorySession::AdvanceToNode(const FName TargetNodeId)
{
    const FNarrRailNode* Node = FindNode(TargetNodeId);
    if (Node == nullptr)
    {
        SessionState = ENarrRailSessionState::Error;
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::MissingNode, TEXT("Target node not found."), TargetNodeId);
    }

    if (ExhaustivePendingChoiceReturnStack.Num() > 0 &&
        ExhaustivePendingChoiceReturnStack.Last() == TargetNodeId)
    {
        ExhaustivePendingChoiceReturnStack.Pop();
    }

    Context.CurrentNodeId = TargetNodeId;
    Context.NodeHistory.Add(Context.CurrentNodeId);

    FString ActionError;
    if (!ExecuteActions(Node->EnterActions, ActionError))
    {
        SessionState = ENarrRailSessionState::Error;
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidInput, *ActionError, Context.CurrentNodeId);
    }

    if (Node->NodeType == ENarrRailNodeType::MultiDialogue)
    {
        CurrentMultiDialogueLineIndex = 0;
    }
    else
    {
        CurrentMultiDialogueLineIndex = INDEX_NONE;
    }

    // 通知 UI 显示器
    UObject* PresenterObject = DialoguePresenter.GetObject();
    const bool bPresenterValid =
        PresenterObject != nullptr &&
        PresenterObject->GetClass()->ImplementsInterface(UNarrRailDialoguePresenterInterface::StaticClass());

    FNarrRailNode EnteredNodeForEvent = *Node;

    // Choice 节点：提前计算可用选项，避免两个 Choice 块重复调用
    TArray<int32> ChoiceAvailableIndices;
    TArray<int32> ChoiceVisibleIndices;
    TArray<FNarrRailChoiceOption> ChoiceVisibleOptions;
    const bool bIsChoiceNode = (Node->NodeType == ENarrRailNodeType::Choice);
    if (bIsChoiceNode)
    {
        ChoiceAvailableIndices = BuildAvailableChoiceIndices(*Node);
        ChoiceVisibleIndices = BuildVisibleChoiceIndices(*Node);
        ChoiceVisibleOptions = BuildVisibleChoiceOptions(*Node);
    }

    if (bPresenterValid)
    {
        if (Node->NodeType == ENarrRailNodeType::Dialogue)
        {
            // 构建对话显示请求
            FNarrRailDialogueRequest Request;
            Request.NodeId = TargetNodeId;
            Request.SpeakerId = Node->Dialogue.SpeakerId;
            Request.TextContent = Node->Dialogue.TextKey;
            Request.SpeechRate = Node->Dialogue.SpeechRate;
            Request.VoiceAsset = Node->Dialogue.VoiceAsset;
            Request.bAutoAdvance = false;

            // 调用 UI 显示对话
            INarrRailDialoguePresenterInterface::Execute_ShowDialogue(PresenterObject, Request);
        }
        else if (Node->NodeType == ENarrRailNodeType::MultiDialogue)
        {
            FNarrRailDialogueRequest Request;
            if (BuildMultiDialogueDisplay(*Node, Request))
            {
                INarrRailDialoguePresenterInterface::Execute_ShowDialogue(PresenterObject, Request);

                EnteredNodeForEvent.NodeType = ENarrRailNodeType::Dialogue;
                EnteredNodeForEvent.Dialogue.SpeakerId = Request.SpeakerId;
                EnteredNodeForEvent.Dialogue.TextKey = Request.TextContent;
                EnteredNodeForEvent.Dialogue.SpeechRate = Request.SpeechRate;
            }
        }
        else if (bIsChoiceNode && !(Node->ChoiceMode == ENarrRailChoiceMode::ExhaustiveUntilComplete && ChoiceAvailableIndices.Num() == 0))
        {
            // 构建选项显示请求（使用预计算的可见选项）
            FNarrRailChoiceRequest Request;
            Request.NodeId = TargetNodeId;
            Request.Choices = ChoiceVisibleOptions;
            Request.Session = this;

            // 调用 UI 显示选项
            INarrRailDialoguePresenterInterface::Execute_ShowChoices(PresenterObject, Request);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[NarrRail] DialoguePresenter is invalid for interface call. Object=%s, InterfacePtrValid=%s"),
            PresenterObject ? *PresenterObject->GetName() : TEXT("None"),
            DialoguePresenter.GetInterface() ? TEXT("true") : TEXT("false"));
    }

    // 触发节点进入事件
    OnNodeEntered.Broadcast(Context.CurrentNodeId, EnteredNodeForEvent);

    if (Node->NodeType == ENarrRailNodeType::End)
    {
        FName ReturnNodeId;
        if (TryPopExhaustiveReturn(ReturnNodeId))
        {
            return AdvanceToNode(ReturnNodeId);
        }

        SessionState = ENarrRailSessionState::Completed;
        // 触发会话结束事件
        OnSessionEnded.Broadcast(Context.CurrentNodeId, SessionState);
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Completed, TEXT("Reached end node."), Context.CurrentNodeId);
    }

    if (bIsChoiceNode)
    {
        if (Node->ChoiceMode == ENarrRailChoiceMode::ExhaustiveUntilComplete && ChoiceAvailableIndices.Num() == 0)
        {
            while (ExhaustivePendingChoiceReturnStack.Num() > 0 &&
                ExhaustivePendingChoiceReturnStack.Last() == Context.CurrentNodeId)
            {
                ExhaustivePendingChoiceReturnStack.Pop();
            }

            if (Node->ChoiceCompletionTargetNodeId == NAME_None)
            {
                SessionState = ENarrRailSessionState::Error;
                return FNarrRailRuntimeResult::Make(
                    ENarrRailRuntimeResultCode::InvalidInput,
                    TEXT("Exhaustive choice node requires ChoiceCompletionTargetNodeId when all choices are consumed."),
                    Context.CurrentNodeId);
            }

            SessionState = ENarrRailSessionState::Running;
            return AdvanceToNode(Node->ChoiceCompletionTargetNodeId);
        }

        RuntimeVisibleChoiceIndexMap.Add(Context.CurrentNodeId, ChoiceVisibleIndices);

        SessionState = ENarrRailSessionState::WaitingForChoice;
        OnChoicesReady.Broadcast(Context.CurrentNodeId, ChoiceVisibleOptions);
    }
    else
    {
        RuntimeVisibleChoiceIndexMap.Remove(Context.CurrentNodeId);
        SessionState = ENarrRailSessionState::Running;
    }

    if (Node->NodeType == ENarrRailNodeType::SetVariable)
    {
        return Next();
    }

    return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Success, TEXT("Advanced to node."), Context.CurrentNodeId);
}

TArray<int32> UNarrRailStorySession::BuildAvailableChoiceIndices(const FNarrRailNode& ChoiceNode) const
{
    TArray<int32> Available;

    const TSet<int32>* Selected = ExhaustiveSelectedChoiceIndices.Find(ChoiceNode.NodeId);

    for (int32 Index = 0; Index < ChoiceNode.Choices.Num(); ++Index)
    {
        if (Selected != nullptr && Selected->Contains(Index))
        {
            continue;
        }

        Available.Add(Index);
    }

    return Available;
}

TArray<int32> UNarrRailStorySession::BuildVisibleChoiceIndices(const FNarrRailNode& ChoiceNode) const
{
    TArray<int32> Visible;
    const TSet<int32>* Selected = ExhaustiveSelectedChoiceIndices.Find(ChoiceNode.NodeId);
    const bool bExhaustiveMode = (ChoiceNode.ChoiceMode == ENarrRailChoiceMode::ExhaustiveUntilComplete);

    for (int32 Index = 0; Index < ChoiceNode.Choices.Num(); ++Index)
    {
        // SinglePass: 仅显示当前可选项；Exhaustive: 显示所有可见项，并通过 bHasBeenSelected 标记已选状态。
        if (!bExhaustiveMode && Selected != nullptr && Selected->Contains(Index))
        {
            continue;
        }

        Visible.Add(Index);
    }

    return Visible;
}

TArray<FNarrRailChoiceOption> UNarrRailStorySession::BuildVisibleChoiceOptions(const FNarrRailNode& ChoiceNode) const
{
    TArray<FNarrRailChoiceOption> VisibleChoices;
    const TArray<int32> VisibleIndices = BuildVisibleChoiceIndices(ChoiceNode);
    const TSet<int32>* Selected = ExhaustiveSelectedChoiceIndices.Find(ChoiceNode.NodeId);

    for (const int32 Index : VisibleIndices)
    {
        if (ChoiceNode.Choices.IsValidIndex(Index))
        {
            FNarrRailChoiceOption VisibleChoice = ChoiceNode.Choices[Index];
            VisibleChoice.bHasBeenSelected = (Selected != nullptr && Selected->Contains(Index));
            VisibleChoices.Add(MoveTemp(VisibleChoice));
        }
    }

    return VisibleChoices;
}

FNarrRailRuntimeResult UNarrRailStorySession::ResolveNextByEdge(const FNarrRailNode& FromNode, FName& OutNextNodeId) const
{
    if (StoryAsset == nullptr)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::InvalidState, TEXT("Session not initialized."));
    }

    TArray<const FNarrRailNodeEdge*> CandidateEdges;
    for (const FNarrRailNodeEdge& Edge : StoryAsset->Edges)
    {
        if (Edge.SourceNodeId == FromNode.NodeId)
        {
            CandidateEdges.Add(&Edge);
        }
    }

    if (CandidateEdges.Num() == 0)
    {
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Completed, TEXT("No outgoing edge. Session completed."), FromNode.NodeId);
    }

    Algo::SortBy(CandidateEdges, [](const FNarrRailNodeEdge* Edge) { return Edge->Priority; });

    if (FromNode.NodeType == ENarrRailNodeType::Condition)
    {
        auto FindEdgeByHandle = [&CandidateEdges](const FName Handle) -> const FNarrRailNodeEdge*
        {
            for (const FNarrRailNodeEdge* Edge : CandidateEdges)
            {
                if (Edge->SourceHandle == Handle)
                {
                    return Edge;
                }
            }

            return nullptr;
        };

        if (FromNode.Condition.Branches.Num() > 0)
        {
            for (int32 BranchIndex = 0; BranchIndex < FromNode.Condition.Branches.Num(); ++BranchIndex)
            {
                const FNarrRailConditionBranch& Branch = FromNode.Condition.Branches[BranchIndex];
                if (!EvaluateConditionBranch(Branch))
                {
                    continue;
                }

                const FName ExpectedHandle(*FString::Printf(TEXT("condition-%d"), BranchIndex));
                const FNarrRailNodeEdge* MatchedEdge = FindEdgeByHandle(ExpectedHandle);

                if (MatchedEdge == nullptr)
                {
                    return FNarrRailRuntimeResult::Make(
                        ENarrRailRuntimeResultCode::InvalidInput,
                        *FString::Printf(TEXT("Condition node missing %s outgoing edge."), *ExpectedHandle.ToString()),
                        FromNode.NodeId);
                }

                OutNextNodeId = MatchedEdge->TargetNodeId;
                return FNarrRailRuntimeResult::Make(
                    ENarrRailRuntimeResultCode::Success,
                    *FString::Printf(TEXT("Resolved %s branch."), *ExpectedHandle.ToString()),
                    OutNextNodeId);
            }

            const FNarrRailNodeEdge* FallbackEdge = FindEdgeByHandle(FName(TEXT("condition-fallback")));

            if (FallbackEdge == nullptr)
            {
                return FNarrRailRuntimeResult::Make(
                    ENarrRailRuntimeResultCode::InvalidInput,
                    TEXT("Condition node missing condition-fallback outgoing edge."),
                    FromNode.NodeId);
            }

            OutNextNodeId = FallbackEdge->TargetNodeId;
            return FNarrRailRuntimeResult::Make(
                ENarrRailRuntimeResultCode::Success,
                TEXT("Resolved condition-fallback branch."),
                OutNextNodeId);
        }

        const bool bConditionResult = EvaluateConditionExpression(FromNode.Condition);
        const FName ExpectedHandle = bConditionResult
            ? FName(TEXT("condition-0"))
            : FName(TEXT("condition-fallback"));

        const FNarrRailNodeEdge* MatchedEdge = FindEdgeByHandle(ExpectedHandle);
        if (MatchedEdge != nullptr)
        {
            OutNextNodeId = MatchedEdge->TargetNodeId;
            return FNarrRailRuntimeResult::Make(
                ENarrRailRuntimeResultCode::Success,
                bConditionResult
                    ? TEXT("Resolved condition true branch.")
                    : TEXT("Resolved condition false branch."),
                OutNextNodeId);
        }

        return FNarrRailRuntimeResult::Make(
            ENarrRailRuntimeResultCode::InvalidInput,
            bConditionResult
                ? TEXT("Condition node missing condition-0 outgoing edge.")
                : TEXT("Condition node missing condition-fallback outgoing edge."),
            FromNode.NodeId);
    }

    for (const FNarrRailNodeEdge* Edge : CandidateEdges)
    {
        OutNextNodeId = Edge->TargetNodeId;
        return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Success, TEXT("Resolved next node by edge."), OutNextNodeId);
    }

    return FNarrRailRuntimeResult::Make(ENarrRailRuntimeResultCode::Completed, TEXT("No outgoing edge. Session completed."), FromNode.NodeId);
}

const FNarrRailNode* UNarrRailStorySession::FindNode(const FName NodeId) const
{
    if (StoryAsset == nullptr || NodeId == NAME_None)
    {
        return nullptr;
    }

    for (const FNarrRailNode& Node : StoryAsset->Nodes)
    {
        if (Node.NodeId == NodeId)
        {
            return &Node;
        }
    }

    return nullptr;
}

bool UNarrRailStorySession::EvaluateConditionExpression(const FNarrRailConditionExpression& Condition) const
{
    if (Condition.Terms.Num() == 0)
    {
        return true;
    }

    if (Condition.Logic == ENarrRailConditionLogic::All)
    {
        for (const FNarrRailConditionTerm& Term : Condition.Terms)
        {
            if (!EvaluateConditionTerm(Term))
            {
                return false;
            }
        }

        return true;
    }

    for (const FNarrRailConditionTerm& Term : Condition.Terms)
    {
        if (EvaluateConditionTerm(Term))
        {
            return true;
        }
    }

    return false;
}

bool UNarrRailStorySession::EvaluateConditionBranch(const FNarrRailConditionBranch& Branch) const
{
    if (Branch.Terms.Num() == 0)
    {
        return true;
    }

    if (Branch.Logic == ENarrRailConditionLogic::All)
    {
        for (const FNarrRailConditionTerm& Term : Branch.Terms)
        {
            if (!EvaluateConditionTerm(Term))
            {
                return false;
            }
        }

        return true;
    }

    for (const FNarrRailConditionTerm& Term : Branch.Terms)
    {
        if (EvaluateConditionTerm(Term))
        {
            return true;
        }
    }

    return false;
}

bool UNarrRailStorySession::EvaluateConditionTerm(const FNarrRailConditionTerm& Term) const
{
    if (Term.Variable.VariableName == NAME_None)
    {
        return false;
    }

    UNarrRailVariableContainer* Container = FindVariableContainerForName(Term.Variable.VariableName);
    if (Container == nullptr)
    {
        return false;
    }

    FString CurrentValue;
    FNarrRailVariableResult GetResult = Container->GetVariable(Term.Variable.VariableName, CurrentValue);
    if (!GetResult.IsSuccess())
    {
        return false;
    }

    switch (Term.Variable.VariableType)
    {
    case ENarrRailVariableType::Bool:
    {
        bool L = false;
        bool R = false;
        if (!NarrRailRuntime::TryParseBool(CurrentValue, L) || !NarrRailRuntime::TryParseBool(Term.CompareValue, R))
        {
            return false;
        }

        if (Term.Operator == ENarrRailComparisonOp::Equal)
        {
            return L == R;
        }
        if (Term.Operator == ENarrRailComparisonOp::NotEqual)
        {
            return L != R;
        }
        return false;
    }
    case ENarrRailVariableType::Int:
    {
        int32 L = 0;
        int32 R = 0;
        if (!NarrRailRuntime::TryParseInt(CurrentValue, L) || !NarrRailRuntime::TryParseInt(Term.CompareValue, R))
        {
            return false;
        }

        switch (Term.Operator)
        {
        case ENarrRailComparisonOp::Equal: return L == R;
        case ENarrRailComparisonOp::NotEqual: return L != R;
        case ENarrRailComparisonOp::Greater: return L > R;
        case ENarrRailComparisonOp::GreaterOrEqual: return L >= R;
        case ENarrRailComparisonOp::Less: return L < R;
        case ENarrRailComparisonOp::LessOrEqual: return L <= R;
        default: return false;
        }
    }
    case ENarrRailVariableType::Float:
    {
        float L = 0.f;
        float R = 0.f;
        if (!NarrRailRuntime::TryParseFloat(CurrentValue, L) || !NarrRailRuntime::TryParseFloat(Term.CompareValue, R))
        {
            return false;
        }

        switch (Term.Operator)
        {
        case ENarrRailComparisonOp::Equal: return FMath::IsNearlyEqual(L, R);
        case ENarrRailComparisonOp::NotEqual: return !FMath::IsNearlyEqual(L, R);
        case ENarrRailComparisonOp::Greater: return L > R;
        case ENarrRailComparisonOp::GreaterOrEqual: return L >= R;
        case ENarrRailComparisonOp::Less: return L < R;
        case ENarrRailComparisonOp::LessOrEqual: return L <= R;
        default: return false;
        }
    }
    case ENarrRailVariableType::String:
    default:
    {
        const int32 Compare = CurrentValue.Compare(Term.CompareValue, ESearchCase::CaseSensitive);
        switch (Term.Operator)
        {
        case ENarrRailComparisonOp::Equal: return Compare == 0;
        case ENarrRailComparisonOp::NotEqual: return Compare != 0;
        case ENarrRailComparisonOp::Greater: return Compare > 0;
        case ENarrRailComparisonOp::GreaterOrEqual: return Compare >= 0;
        case ENarrRailComparisonOp::Less: return Compare < 0;
        case ENarrRailComparisonOp::LessOrEqual: return Compare <= 0;
        default: return false;
        }
    }
    }
}

bool UNarrRailStorySession::ExecuteActions(const TArray<FNarrRailNodeAction>& Actions, FString& OutErrorMessage)
{
    if (VariableContainer == nullptr && GlobalVariableContainer == nullptr)
    {
        OutErrorMessage = TEXT("Variable container not initialized.");
        return false;
    }

    for (const FNarrRailNodeAction& Action : Actions)
    {
        switch (Action.ActionType)
        {
        case ENarrRailActionType::EmitEvent:
            if (Action.EventId != NAME_None)
            {
                Context.EmittedEvents.Add(Action.EventId);
            }
            break;

        case ENarrRailActionType::Set:
        {
            if (Action.Variable.VariableName == NAME_None)
            {
                OutErrorMessage = TEXT("Action variable name is empty.");
                return false;
            }

            UNarrRailVariableContainer* Container = FindVariableContainerForName(Action.Variable.VariableName);
            if (Container == nullptr)
            {
                OutErrorMessage = FString::Printf(TEXT("Variable '%s' not found."), *Action.Variable.VariableName.ToString());
                return false;
            }

            FNarrRailVariableResult Result = Container->SetVariable(Action.Variable.VariableName, Action.Value);
            if (!Result.IsSuccess())
            {
                OutErrorMessage = FString::Printf(TEXT("Set variable failed: %s"), *Result.ErrorMessage);
                return false;
            }
            break;
        }

        case ENarrRailActionType::Add:
        case ENarrRailActionType::Subtract:
        {
            if (Action.Variable.VariableName == NAME_None)
            {
                OutErrorMessage = TEXT("Action variable name is empty.");
                return false;
            }

            UNarrRailVariableContainer* Container = FindVariableContainerForName(Action.Variable.VariableName);
            if (Container == nullptr || !Container->HasVariable(Action.Variable.VariableName))
            {
                OutErrorMessage = FString::Printf(TEXT("Variable '%s' not found."), *Action.Variable.VariableName.ToString());
                return false;
            }

            FNarrRailVariableResult Result;
            const ENarrRailVariableType VariableType = Container->GetVariableType(Action.Variable.VariableName);
            const bool bAdd = Action.ActionType == ENarrRailActionType::Add;
            if (VariableType == ENarrRailVariableType::Int)
            {
                int32 Delta = 0;
                if (!NarrRailRuntime::TryParseInt(Action.Value, Delta))
                {
                    OutErrorMessage = TEXT("Int action parse failed.");
                    return false;
                }
                Result = bAdd
                    ? Container->AddInt(Action.Variable.VariableName, Delta)
                    : Container->SubtractInt(Action.Variable.VariableName, Delta);
            }
            else if (VariableType == ENarrRailVariableType::Float)
            {
                float Delta = 0.f;
                if (!NarrRailRuntime::TryParseFloat(Action.Value, Delta))
                {
                    OutErrorMessage = TEXT("Float action parse failed.");
                    return false;
                }
                Result = bAdd
                    ? Container->AddFloat(Action.Variable.VariableName, Delta)
                    : Container->SubtractFloat(Action.Variable.VariableName, Delta);
            }
            else
            {
                OutErrorMessage = bAdd
                    ? TEXT("Add only supports Int/Float variables.")
                    : TEXT("Subtract only supports Int/Float variables.");
                return false;
            }

            if (!Result.IsSuccess())
            {
                OutErrorMessage = FString::Printf(TEXT("Variable arithmetic failed: %s"), *Result.ErrorMessage);
                return false;
            }
            break;
        }

        default:
            OutErrorMessage = TEXT("Unsupported action type.");
            return false;
        }
    }

    SyncVariableSnapshotToContext();
    return true;
}

// === 全局会话管理 ===

TArray<UNarrRailStorySession*> UNarrRailStorySession::GetAllActiveSessions()
{
    // 清理已销毁的会话
    ActiveSessions.RemoveAll([](UNarrRailStorySession* Session)
    {
        return Session == nullptr || !IsValid(Session);
    });

    return ActiveSessions;
}

void UNarrRailStorySession::SetDebugName(const FString& InDebugName)
{
    DebugName = InDebugName;
}
