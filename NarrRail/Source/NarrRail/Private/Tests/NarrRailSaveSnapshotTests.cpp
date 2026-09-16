// 存档快照的自动化测试。
//
// 位置与写法遵循引擎约定：Private/Tests/ 下 UBT 会自动编译（无需改 Build.cs），
// Misc/AutomationTest.h 属于 Core（模块已有依赖），整个文件由 WITH_DEV_AUTOMATION_TESTS 包住，
// 因此在 Shipping 构建里会被编译掉。运行方式见 specs/0001-save-load-snapshots/quickstart.md §1。
//
// 每个测试都自己搭剧情资产，不依赖 Content/ 里的 .uasset：这样测试可以在 headless 下跑，
// 也不会因为美术改资源而失效。

#include "Misc/AutomationTest.h"

#include "Runtime/NarrRailGlobalConfigAsset.h"
#include "Runtime/NarrRailGlobalStateSubsystem.h"
#include "Runtime/NarrRailStoryAsset.h"
#include "Runtime/NarrRailStorySession.h"
#include "Engine/GameInstance.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace NarrRailSaveSnapshotTests
{
// ============================================================================
// 测试夹具
// ============================================================================

// 夹具剧情：Intro(MultiDialogue, 2 行) -> Plain -> After -> Pick(Choice, 穷举, 3 选项) -> BranchN -> Finish(End)
//
// 这段结构刻意覆盖四种节点形态，让同一个夹具既能量多行对话的行索引，也能量穷举选择的已消费集合，
// 还能量返回栈（BranchN -> Finish 是 End 节点，Next() 会弹出返回栈回到 Pick）。
static const TCHAR* EntryNodeId = TEXT("Intro");
static const TCHAR* PlainNodeId = TEXT("Plain");
static const TCHAR* AfterNodeId = TEXT("After");
static const TCHAR* ChoiceNodeId = TEXT("Pick");
static const TCHAR* FinishNodeId = TEXT("Finish");
static const TCHAR* BranchNodeIdFormat = TEXT("Branch%d");

static FNarrRailVariableDefinition MakeVariable(const FName Name, const ENarrRailVariableType Type, const FString& DefaultValue, const bool bGlobalScope = false)
{
    FNarrRailVariableDefinition Definition;
    Definition.VariableName = Name;
    Definition.VariableType = Type;
    Definition.DefaultValue = DefaultValue;
    Definition.bGlobalScope = bGlobalScope;
    return Definition;
}

static FNarrRailNode MakeDialogueNode(const TCHAR* NodeId, const TCHAR* SpeakerId, const TCHAR* TextKey)
{
    FNarrRailNode Node;
    Node.NodeId = FName(NodeId);
    Node.NodeType = ENarrRailNodeType::Dialogue;
    Node.Dialogue.SpeakerId = FName(SpeakerId);
    Node.Dialogue.TextKey = TextKey;
    return Node;
}

static FNarrRailNode MakeMultiDialogueNode(const TCHAR* NodeId, const int32 LineCount)
{
    FNarrRailNode Node;
    Node.NodeId = FName(NodeId);
    Node.NodeType = ENarrRailNodeType::MultiDialogue;
    Node.MultiDialogue.SpeakerId = FName(TEXT("Narrator"));

    for (int32 LineIndex = 0; LineIndex < LineCount; ++LineIndex)
    {
        FNarrRailDialogueLine Line;
        Line.TextKey = FString::Printf(TEXT("Line%d"), LineIndex);
        Node.MultiDialogue.Lines.Add(Line);
    }

    return Node;
}

static FNarrRailNode MakeChoiceNode(const TCHAR* NodeId, const int32 OptionCount)
{
    FNarrRailNode Node;
    Node.NodeId = FName(NodeId);
    Node.NodeType = ENarrRailNodeType::Choice;

    // ExhaustiveUntilComplete 是触发「已消费选项」与「返回栈」的唯一模式，
    // 也就是本文件里 T012 与 T032 想量的那套状态。
    Node.ChoiceMode = ENarrRailChoiceMode::ExhaustiveUntilComplete;

    for (int32 OptionIndex = 0; OptionIndex < OptionCount; ++OptionIndex)
    {
        FNarrRailChoiceOption Option;
        Option.TextKey = FString::Printf(TEXT("Opt%d"), OptionIndex);
        Option.TargetNodeId = FName(*FString::Printf(BranchNodeIdFormat, OptionIndex));
        Node.Choices.Add(Option);
    }

    return Node;
}

static FNarrRailNode MakeEndNode(const TCHAR* NodeId)
{
    FNarrRailNode Node;
    Node.NodeId = FName(NodeId);
    Node.NodeType = ENarrRailNodeType::End;
    return Node;
}

static void AddEdge(UNarrRailStoryAsset* Story, const TCHAR* SourceNodeId, const TCHAR* TargetNodeId)
{
    FNarrRailNodeEdge Edge;
    Edge.SourceNodeId = FName(SourceNodeId);
    Edge.TargetNodeId = FName(TargetNodeId);
    Story->Edges.Add(Edge);
}

static UNarrRailStoryAsset* MakeStory(const TCHAR* ObjectName, const FName StoryId, const int32 ChoiceOptionCount = 3)
{
    UNarrRailStoryAsset* Story = NewObject<UNarrRailStoryAsset>(GetTransientPackage(), FName(ObjectName));
    Story->StoryId = StoryId;
    Story->EntryNodeId = FName(EntryNodeId);
    Story->Variables.Add(MakeVariable(TEXT("Score"), ENarrRailVariableType::Int, TEXT("0")));

    Story->Nodes.Add(MakeMultiDialogueNode(EntryNodeId, 2));
    Story->Nodes.Add(MakeDialogueNode(PlainNodeId, TEXT("Alice"), TEXT("PlainText")));
    Story->Nodes.Add(MakeDialogueNode(AfterNodeId, TEXT("Alice"), TEXT("AfterText")));
    Story->Nodes.Add(MakeChoiceNode(ChoiceNodeId, ChoiceOptionCount));

    for (int32 OptionIndex = 0; OptionIndex < ChoiceOptionCount; ++OptionIndex)
    {
        const FString BranchId = FString::Printf(BranchNodeIdFormat, OptionIndex);
        Story->Nodes.Add(MakeDialogueNode(*BranchId, TEXT("Alice"), *FString::Printf(TEXT("BranchText%d"), OptionIndex)));
        AddEdge(Story, *BranchId, FinishNodeId);
    }

    Story->Nodes.Add(MakeEndNode(FinishNodeId));

    AddEdge(Story, EntryNodeId, PlainNodeId);
    AddEdge(Story, PlainNodeId, AfterNodeId);
    AddEdge(Story, AfterNodeId, ChoiceNodeId);

    return Story;
}

// 全局配置必须活在一个真实的 package 里：快照记的是 FSoftObjectPath，而无路径的对象没有可恢复的
// 身份，恢复阶段的 TryLoad 也就无从解析。
static UNarrRailGlobalConfigAsset* MakeGlobalConfig(const TCHAR* PackageName, const TCHAR* ObjectName)
{
    UPackage* Package = CreatePackage(PackageName);
    UNarrRailGlobalConfigAsset* Config = NewObject<UNarrRailGlobalConfigAsset>(Package, FName(ObjectName), RF_Public);
    Config->Variables.Add(MakeVariable(TEXT("Trust"), ENarrRailVariableType::Int, TEXT("0"), true));

    FNarrRailPresetSpeaker Speaker;
    Speaker.SpeakerId = TEXT("Alice");
    Speaker.DisplayName = TEXT("Alice Display");
    Speaker.Color = FLinearColor::Red;
    Config->PresetSpeakers.Add(Speaker);

    return Config;
}

// 把会话推进到指定节点。返回是否成功走到。
static bool PlayToNode(FAutomationTestBase& Test, UNarrRailStorySession* Session, const FName TargetNodeId, const int32 MaxSteps = 16)
{
    for (int32 Step = 0; Step < MaxSteps; ++Step)
    {
        if (Session->GetCurrentNodeId() == TargetNodeId)
        {
            return true;
        }

        const FNarrRailRuntimeResult Result = Session->Next();
        if (Result.Code != ENarrRailRuntimeResultCode::Success)
        {
            Test.AddError(FString::Printf(
                TEXT("PlayToNode('%s') stopped at '%s' with code %d: %s"),
                *TargetNodeId.ToString(),
                *Session->GetCurrentNodeId().ToString(),
                static_cast<int32>(Result.Code),
                *Result.Message));
            return false;
        }
    }

    Test.AddError(FString::Printf(TEXT("PlayToNode('%s') exceeded %d steps, stuck at '%s'."), *TargetNodeId.ToString(), MaxSteps, *Session->GetCurrentNodeId().ToString()));
    return false;
}

// ============================================================================
// 比较工具
// ============================================================================

static bool ChoiceSelectionsEqual(const TArray<FNarrRailChoiceSelectionSnapshot>& Left, const TArray<FNarrRailChoiceSelectionSnapshot>& Right, FString& OutDifference)
{
    if (Left.Num() != Right.Num())
    {
        OutDifference = FString::Printf(TEXT("ExhaustiveChoiceSelections count %d vs %d"), Left.Num(), Right.Num());
        return false;
    }

    for (int32 Index = 0; Index < Left.Num(); ++Index)
    {
        if (Left[Index].ChoiceNodeId != Right[Index].ChoiceNodeId)
        {
            OutDifference = FString::Printf(TEXT("ExhaustiveChoiceSelections[%d].ChoiceNodeId '%s' vs '%s'"),
                Index, *Left[Index].ChoiceNodeId.ToString(), *Right[Index].ChoiceNodeId.ToString());
            return false;
        }

        if (Left[Index].SelectedChoiceIndices != Right[Index].SelectedChoiceIndices)
        {
            OutDifference = FString::Printf(TEXT("ExhaustiveChoiceSelections[%d].SelectedChoiceIndices differ on node '%s'"),
                Index, *Left[Index].ChoiceNodeId.ToString());
            return false;
        }
    }

    return true;
}

// 逐字段比较两个快照，并指出第一个不同之处。
//
// USTRUCT 没有 operator==，所以只能手写。刻意把 14 个字段全部列出来而不是挑几个比：这个函数的
// 用途之一就是证明「采集不改变状态」，漏掉一个字段就等于留下一个没人看的缺口。
static bool SnapshotsEqual(const FNarrRailStorySessionSnapshot& Left, const FNarrRailStorySessionSnapshot& Right, FString& OutDifference)
{
    if (Left.SnapshotVersion != Right.SnapshotVersion)
    {
        OutDifference = FString::Printf(TEXT("SnapshotVersion %d vs %d"), Left.SnapshotVersion, Right.SnapshotVersion);
        return false;
    }

    if (Left.StoryId != Right.StoryId)
    {
        OutDifference = FString::Printf(TEXT("StoryId '%s' vs '%s'"), *Left.StoryId.ToString(), *Right.StoryId.ToString());
        return false;
    }

    if (Left.StoryAssetPath != Right.StoryAssetPath)
    {
        OutDifference = FString::Printf(TEXT("StoryAssetPath '%s' vs '%s'"), *Left.StoryAssetPath.ToString(), *Right.StoryAssetPath.ToString());
        return false;
    }

    if (Left.GlobalConfigPath != Right.GlobalConfigPath)
    {
        OutDifference = FString::Printf(TEXT("GlobalConfigPath '%s' vs '%s'"), *Left.GlobalConfigPath.ToString(), *Right.GlobalConfigPath.ToString());
        return false;
    }

    if (Left.SessionState != Right.SessionState)
    {
        OutDifference = FString::Printf(TEXT("SessionState %d vs %d"), static_cast<int32>(Left.SessionState), static_cast<int32>(Right.SessionState));
        return false;
    }

    if (Left.StateBeforePause != Right.StateBeforePause)
    {
        OutDifference = FString::Printf(TEXT("StateBeforePause %d vs %d"), static_cast<int32>(Left.StateBeforePause), static_cast<int32>(Right.StateBeforePause));
        return false;
    }

    if (Left.CurrentNodeId != Right.CurrentNodeId)
    {
        OutDifference = FString::Printf(TEXT("CurrentNodeId '%s' vs '%s'"), *Left.CurrentNodeId.ToString(), *Right.CurrentNodeId.ToString());
        return false;
    }

    if (Left.NodeHistory != Right.NodeHistory)
    {
        OutDifference = FString::Printf(TEXT("NodeHistory %d entries vs %d entries"), Left.NodeHistory.Num(), Right.NodeHistory.Num());
        return false;
    }

    if (Left.EmittedEvents != Right.EmittedEvents)
    {
        OutDifference = FString::Printf(TEXT("EmittedEvents %d entries vs %d entries"), Left.EmittedEvents.Num(), Right.EmittedEvents.Num());
        return false;
    }

    if (Left.LocalVariableSnapshot.Num() != Right.LocalVariableSnapshot.Num())
    {
        OutDifference = FString::Printf(TEXT("LocalVariableSnapshot %d entries vs %d entries"), Left.LocalVariableSnapshot.Num(), Right.LocalVariableSnapshot.Num());
        return false;
    }

    for (const TPair<FName, FString>& Pair : Left.LocalVariableSnapshot)
    {
        const FString* RightValue = Right.LocalVariableSnapshot.Find(Pair.Key);
        if (RightValue == nullptr || *RightValue != Pair.Value)
        {
            OutDifference = FString::Printf(TEXT("LocalVariableSnapshot['%s'] differs"), *Pair.Key.ToString());
            return false;
        }
    }

    if (Left.CurrentMultiDialogueLineIndex != Right.CurrentMultiDialogueLineIndex)
    {
        OutDifference = FString::Printf(TEXT("CurrentMultiDialogueLineIndex %d vs %d"), Left.CurrentMultiDialogueLineIndex, Right.CurrentMultiDialogueLineIndex);
        return false;
    }

    if (Left.LastChoiceInfo.ChoiceNodeId != Right.LastChoiceInfo.ChoiceNodeId ||
        Left.LastChoiceInfo.ChoiceIndex != Right.LastChoiceInfo.ChoiceIndex ||
        Left.LastChoiceInfo.TargetNodeId != Right.LastChoiceInfo.TargetNodeId ||
        Left.LastChoiceInfo.ChoiceTextKey != Right.LastChoiceInfo.ChoiceTextKey ||
        Left.LastChoiceInfo.bValid != Right.LastChoiceInfo.bValid)
    {
        OutDifference = TEXT("LastChoiceInfo differs");
        return false;
    }

    if (!ChoiceSelectionsEqual(Left.ExhaustiveChoiceSelections, Right.ExhaustiveChoiceSelections, OutDifference))
    {
        return false;
    }

    if (Left.ExhaustivePendingChoiceReturnStack != Right.ExhaustivePendingChoiceReturnStack)
    {
        OutDifference = FString::Printf(TEXT("ExhaustivePendingChoiceReturnStack %d entries vs %d entries"),
            Left.ExhaustivePendingChoiceReturnStack.Num(), Right.ExhaustivePendingChoiceReturnStack.Num());
        return false;
    }

    return true;
}

// 会话对外可观测的状态。用于断言「被拒绝的恢复一点都没改动会话」。
struct FObservableSessionState
{
    FName CurrentNodeId = NAME_None;
    int32 LineIndex = INDEX_NONE;
    ENarrRailSessionState SessionState = ENarrRailSessionState::Idle;
    int32 HistoryCount = 0;
    bool bScoreReadable = false;
    int32 Score = 0;

    static FObservableSessionState Capture(const UNarrRailStorySession* Session)
    {
        FObservableSessionState State;
        State.CurrentNodeId = Session->GetCurrentNodeId();
        State.LineIndex = Session->GetCurrentMultiDialogueLineIndex();
        State.SessionState = Session->GetSessionState();
        State.HistoryCount = Session->GetHistory().Num();

        int32 Value = 0;
        State.bScoreReadable = Session->GetVariableInt(TEXT("Score"), Value).IsSuccess();
        State.Score = Value;
        return State;
    }

    bool Matches(const FObservableSessionState& Other, FString& OutDifference) const
    {
        if (CurrentNodeId != Other.CurrentNodeId)
        {
            OutDifference = FString::Printf(TEXT("CurrentNodeId '%s' vs '%s'"), *CurrentNodeId.ToString(), *Other.CurrentNodeId.ToString());
            return false;
        }
        if (LineIndex != Other.LineIndex)
        {
            OutDifference = FString::Printf(TEXT("LineIndex %d vs %d"), LineIndex, Other.LineIndex);
            return false;
        }
        if (SessionState != Other.SessionState)
        {
            OutDifference = FString::Printf(TEXT("SessionState %d vs %d"), static_cast<int32>(SessionState), static_cast<int32>(Other.SessionState));
            return false;
        }
        if (HistoryCount != Other.HistoryCount)
        {
            OutDifference = FString::Printf(TEXT("HistoryCount %d vs %d"), HistoryCount, Other.HistoryCount);
            return false;
        }
        if (bScoreReadable != Other.bScoreReadable)
        {
            OutDifference = TEXT("Score readability differs");
            return false;
        }
        if (bScoreReadable && Score != Other.Score)
        {
            OutDifference = FString::Printf(TEXT("Score %d vs %d"), Score, Other.Score);
            return false;
        }
        return true;
    }
};

// 把会话准备到「已经走到 Plain 节点、Score 已改写」的状态，供多个用例共用。
// RestoreTarget 是恢复目标会话，Source 是产出快照的会话。
static UNarrRailStorySession* MakeSessionAtPlain(FAutomationTestBase& Test, UNarrRailStoryAsset* Story, const TCHAR* DebugLabel)
{
    UNarrRailStorySession* Session = NewObject<UNarrRailStorySession>(GetTransientPackage());
    Test.TestEqual(FString::Printf(TEXT("%s Initialize succeeds"), DebugLabel), Session->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);
    Test.TestEqual(FString::Printf(TEXT("%s Start succeeds"), DebugLabel), Session->Start().Code, ENarrRailRuntimeResultCode::Success);

    if (!PlayToNode(Test, Session, FName(PlainNodeId)))
    {
        return nullptr;
    }

    return Session;
}

// 断言「被拒绝的恢复一点都没改动会话」。
//
// 先比较、再拼消息。反过来写会让 %s 在比较发生之前就被求值，于是失败信息永远打印一个空串——
// 断言仍然会红，但看不出是哪个字段变了，而排查这类问题全靠那一个字段名。
static void AssertSessionUnchanged(FAutomationTestBase& Test, const TCHAR* Label, const FObservableSessionState& Before, const UNarrRailStorySession* Session)
{
    const FObservableSessionState After = FObservableSessionState::Capture(Session);
    FString Difference;
    const bool bUnchanged = Before.Matches(After, Difference);
    Test.TestTrue(FString::Printf(TEXT("%s leaves the session unchanged (%s)"), Label, *Difference), bUnchanged);
}

// 同上：先比较，再拼消息。
static void AssertSnapshotsEqual(FAutomationTestBase& Test, const TCHAR* Label, const FNarrRailStorySessionSnapshot& Left, const FNarrRailStorySessionSnapshot& Right)
{
    FString Difference;
    const bool bEqual = SnapshotsEqual(Left, Right, Difference);
    Test.TestTrue(FString::Printf(TEXT("%s (%s)"), Label, *Difference), bEqual);
}
}

// ============================================================================
// T010 - 普通节点往返
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrRailSessionSnapshotPlainNodeRoundTripTest,
    "NarrRail.Save.SessionSnapshot.PlainNodeRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNarrRailSessionSnapshotPlainNodeRoundTripTest::RunTest(const FString& Parameters)
{
    using namespace NarrRailSaveSnapshotTests;

    UNarrRailStoryAsset* Story = MakeStory(TEXT("NR_SaveSnapshot_PlainRoundTrip"), TEXT("PlainRoundTrip"));

    UNarrRailStorySession* SourceSession = MakeSessionAtPlain(*this, Story, TEXT("Source"));
    if (SourceSession == nullptr)
    {
        return false;
    }

    TestTrue(TEXT("Source Score write succeeds"), SourceSession->SetVariableInt(TEXT("Score"), 7).IsSuccess());
    TestEqual(TEXT("Source sits on the plain node"), SourceSession->GetCurrentNodeId(), FName(PlainNodeId));
    TestEqual(TEXT("Plain node clears the multi-dialogue line index"), SourceSession->GetCurrentMultiDialogueLineIndex(), INDEX_NONE);

    const FNarrRailStorySessionSnapshot Snapshot = SourceSession->GetSessionSnapshot();

    // 恢复目标先被写坏，这样「恢复成功」就不会被误判成「本来就是这个值」。
    UNarrRailStorySession* RestoredSession = MakeSessionAtPlain(*this, Story, TEXT("Restored"));
    if (RestoredSession == nullptr)
    {
        return false;
    }

    TestTrue(TEXT("Restored pre-state can be changed"), RestoredSession->SetVariableInt(TEXT("Score"), 1).IsSuccess());

    const FNarrRailRuntimeResult RestoreResult = RestoredSession->RestoreSessionSnapshot(Snapshot, false);
    TestEqual(TEXT("Restore succeeds"), RestoreResult.Code, ENarrRailRuntimeResultCode::Success);
    TestEqual(TEXT("Current node restored"), RestoredSession->GetCurrentNodeId(), FName(PlainNodeId));
    TestEqual(TEXT("Session state restored"), RestoredSession->GetSessionState(), ENarrRailSessionState::Running);

    int32 RestoredScore = 0;
    TestTrue(TEXT("Restored Score can be read"), RestoredSession->GetVariableInt(TEXT("Score"), RestoredScore).IsSuccess());
    TestEqual(TEXT("Local variable value restored"), RestoredScore, 7);

    return true;
}

// ============================================================================
// T011 - MultiDialogue 中途往返，且恢复的是那一行而不是第一行
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrRailSessionSnapshotMultiDialogueLineRoundTripTest,
    "NarrRail.Save.SessionSnapshot.MultiDialogueLineRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNarrRailSessionSnapshotMultiDialogueLineRoundTripTest::RunTest(const FString& Parameters)
{
    using namespace NarrRailSaveSnapshotTests;

    UNarrRailStoryAsset* Story = MakeStory(TEXT("NR_SaveSnapshot_LineRoundTrip"), TEXT("LineRoundTrip"));

    UNarrRailStorySession* SourceSession = NewObject<UNarrRailStorySession>(GetTransientPackage());
    TestEqual(TEXT("Source Initialize succeeds"), SourceSession->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);
    TestEqual(TEXT("Source Start succeeds"), SourceSession->Start().Code, ENarrRailRuntimeResultCode::Success);
    TestEqual(TEXT("Source is on the first line"), SourceSession->GetCurrentMultiDialogueLineIndex(), 0);

    TestEqual(TEXT("Source advances inside multi-dialogue"), SourceSession->Next().Code, ENarrRailRuntimeResultCode::Success);
    TestEqual(TEXT("Source is on a later line"), SourceSession->GetCurrentMultiDialogueLineIndex(), 1);

    const FNarrRailStorySessionSnapshot Snapshot = SourceSession->GetSessionSnapshot();
    TestEqual(TEXT("Snapshot records the resumed line"), Snapshot.CurrentMultiDialogueLineIndex, 1);

    UNarrRailStorySession* RestoredSession = NewObject<UNarrRailStorySession>(GetTransientPackage());
    TestEqual(TEXT("Restored Initialize succeeds"), RestoredSession->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);
    TestEqual(TEXT("Restored Start succeeds"), RestoredSession->Start().Code, ENarrRailRuntimeResultCode::Success);

    const FNarrRailRuntimeResult RestoreResult = RestoredSession->RestoreSessionSnapshot(Snapshot, false);
    TestEqual(TEXT("Restore succeeds"), RestoreResult.Code, ENarrRailRuntimeResultCode::Success);
    TestEqual(TEXT("Restored line index is the captured one"), RestoredSession->GetCurrentMultiDialogueLineIndex(), 1);

    // 这一条是这个用例的重点：恢复成「节点的第一行」在只比较节点 Id 的断言下会完全看不出来。
    TestTrue(TEXT("Restored line index is not the first line"),
        RestoredSession->GetCurrentMultiDialogueLineIndex() != 0);
    TestEqual(TEXT("Restored line is on the captured node"), RestoredSession->GetCurrentNodeId(), FName(EntryNodeId));

    return true;
}

// ============================================================================
// T012 - 部分消费的 Choice 节点往返
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrRailSessionSnapshotConsumedChoiceRoundTripTest,
    "NarrRail.Save.SessionSnapshot.ConsumedChoiceRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNarrRailSessionSnapshotConsumedChoiceRoundTripTest::RunTest(const FString& Parameters)
{
    using namespace NarrRailSaveSnapshotTests;

    UNarrRailStoryAsset* Story = MakeStory(TEXT("NR_SaveSnapshot_ChoiceRoundTrip"), TEXT("ChoiceRoundTrip"));

    UNarrRailStorySession* SourceSession = NewObject<UNarrRailStorySession>(GetTransientPackage());
    TestEqual(TEXT("Source Initialize succeeds"), SourceSession->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);
    TestEqual(TEXT("Source Start succeeds"), SourceSession->Start().Code, ENarrRailRuntimeResultCode::Success);
    TestTrue(TEXT("Source walks to the choice node"), PlayToNode(*this, SourceSession, FName(ChoiceNodeId)));
    TestEqual(TEXT("Source waits for a choice"), SourceSession->GetSessionState(), ENarrRailSessionState::WaitingForChoice);

    TestEqual(TEXT("Source picks the first option"), SourceSession->Choose(0).Code, ENarrRailRuntimeResultCode::Success);
    TestEqual(TEXT("Source lands on the first branch"), SourceSession->GetCurrentNodeId(), FName(TEXT("Branch0")));

    const FNarrRailStorySessionSnapshot Snapshot = SourceSession->GetSessionSnapshot();

    // 采集把 TSet 转成了每节点一条的排序记录，未消费的节点不该出现在里面。
    TestEqual(TEXT("Exactly one consumed choice record"), Snapshot.ExhaustiveChoiceSelections.Num(), 1);
    TestEqual(TEXT("Consumed record points at the choice node"), Snapshot.ExhaustiveChoiceSelections[0].ChoiceNodeId, FName(ChoiceNodeId));
    TestEqual(TEXT("Consumed record holds one index"), Snapshot.ExhaustiveChoiceSelections[0].SelectedChoiceIndices.Num(), 1);
    TestEqual(TEXT("Consumed index is the picked one"), Snapshot.ExhaustiveChoiceSelections[0].SelectedChoiceIndices[0], 0);
    TestEqual(TEXT("Return stack records the choice node for the exhaustive branch"), Snapshot.ExhaustivePendingChoiceReturnStack.Num(), 1);

    UNarrRailStorySession* RestoredSession = NewObject<UNarrRailStorySession>(GetTransientPackage());
    TestEqual(TEXT("Restored Initialize succeeds"), RestoredSession->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);

    const FNarrRailRuntimeResult RestoreResult = RestoredSession->RestoreSessionSnapshot(Snapshot, false);
    TestEqual(TEXT("Restore succeeds"), RestoreResult.Code, ENarrRailRuntimeResultCode::Success);
    TestEqual(TEXT("Restored node is the branch the choice led to"), RestoredSession->GetCurrentNodeId(), FName(TEXT("Branch0")));

    // 再采集一次并逐字段比对：已消费集合与返回栈都必须活过这一趟。
    const FNarrRailStorySessionSnapshot Recaptured = RestoredSession->GetSessionSnapshot();
    TestEqual(TEXT("Recaptured still has one consumed choice record"), Recaptured.ExhaustiveChoiceSelections.Num(), 1);
    TestEqual(TEXT("Recaptured consumed node survives"), Recaptured.ExhaustiveChoiceSelections[0].ChoiceNodeId, FName(ChoiceNodeId));
    TestEqual(TEXT("Recaptured consumed indices survive"), Recaptured.ExhaustiveChoiceSelections[0].SelectedChoiceIndices.Num(), 1);
    TestEqual(TEXT("Recaptured return stack survives"), Recaptured.ExhaustivePendingChoiceReturnStack.Num(), 1);

    FString Difference;
    const bool bRoundTripMatches = SnapshotsEqual(Snapshot, Recaptured, Difference);
    TestTrue(FString::Printf(TEXT("Round-tripped snapshot matches the original (%s)"), *Difference), bRoundTripMatches);

    return true;
}

// ============================================================================
// T013 - 采集不改变会话状态（FR-006 / SC-002）
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrRailSessionSnapshotCaptureDoesNotMutateTest,
    "NarrRail.Save.SessionSnapshot.CaptureDoesNotMutate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNarrRailSessionSnapshotCaptureDoesNotMutateTest::RunTest(const FString& Parameters)
{
    using namespace NarrRailSaveSnapshotTests;

    UNarrRailStoryAsset* Story = MakeStory(TEXT("NR_SaveSnapshot_NoMutate"), TEXT("NoMutate"));

    // 停在一个「状态最丰富」的点上：有行索引、有历史、有已改写变量、有已消费选择与返回栈。
    UNarrRailStorySession* Session = NewObject<UNarrRailStorySession>(GetTransientPackage());
    TestEqual(TEXT("Initialize succeeds"), Session->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);
    TestEqual(TEXT("Start succeeds"), Session->Start().Code, ENarrRailRuntimeResultCode::Success);
    TestTrue(TEXT("Session walks to the choice node"), PlayToNode(*this, Session, FName(ChoiceNodeId)));
    TestEqual(TEXT("Session picks the first option"), Session->Choose(0).Code, ENarrRailRuntimeResultCode::Success);
    TestTrue(TEXT("Score write succeeds"), Session->SetVariableInt(TEXT("Score"), 7).IsSuccess());

    const FObservableSessionState StateBefore = FObservableSessionState::Capture(Session);
    const FNarrRailStorySessionSnapshot FirstSnapshot = Session->GetSessionSnapshot();
    const FObservableSessionState StateAfterFirstCapture = FObservableSessionState::Capture(Session);

    FString Difference;
    const bool bUnchanged = StateBefore.Matches(StateAfterFirstCapture, Difference);
    TestTrue(FString::Printf(TEXT("First capture left the session untouched (%s)"), *Difference), bUnchanged);

    const FNarrRailStorySessionSnapshot SecondSnapshot = Session->GetSessionSnapshot();

    AssertSnapshotsEqual(*this, TEXT("Two captures of one state are equal"), FirstSnapshot, SecondSnapshot);

    // 版本必须被显式盖章。默认值是 0，也就是「缺失」的哨兵，采集绝不能产出它。
    TestEqual(TEXT("Capture stamps a supported snapshot version"), FirstSnapshot.SnapshotVersion, 1);

    return true;
}

// ============================================================================
// T014 - 恢复后的会话推进结果与未中断的会话一致
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrRailSessionSnapshotResumeAdvancesIdenticallyTest,
    "NarrRail.Save.SessionSnapshot.ResumeAdvancesIdentically",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNarrRailSessionSnapshotResumeAdvancesIdenticallyTest::RunTest(const FString& Parameters)
{
    using namespace NarrRailSaveSnapshotTests;

    UNarrRailStoryAsset* Story = MakeStory(TEXT("NR_SaveSnapshot_Advance"), TEXT("Advance"));

    UNarrRailStorySession* ReferenceSession = MakeSessionAtPlain(*this, Story, TEXT("Reference"));
    if (ReferenceSession == nullptr)
    {
        return false;
    }

    const FNarrRailStorySessionSnapshot Snapshot = ReferenceSession->GetSessionSnapshot();

    // 未中断的会话从这里继续走。
    TestEqual(TEXT("Reference advances"), ReferenceSession->Next().Code, ENarrRailRuntimeResultCode::Success);
    const FName ExpectedNextNodeId = ReferenceSession->GetCurrentNodeId();

    UNarrRailStorySession* RestoredSession = MakeSessionAtPlain(*this, Story, TEXT("Restored"));
    if (RestoredSession == nullptr)
    {
        return false;
    }

    const FNarrRailRuntimeResult RestoreResult = RestoredSession->RestoreSessionSnapshot(Snapshot, false);
    TestEqual(TEXT("Restore succeeds"), RestoreResult.Code, ENarrRailRuntimeResultCode::Success);

    TestEqual(TEXT("Restored session advances"), RestoredSession->Next().Code, ENarrRailRuntimeResultCode::Success);
    TestEqual(TEXT("Restored session reaches the same next node"), RestoredSession->GetCurrentNodeId(), ExpectedNextNodeId);

    return true;
}

// ============================================================================
// T020-T022 - 版本门：更新 / 更旧 / 缺失或畸形，一律拒绝且不改状态
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrRailSessionSnapshotRejectUnsupportedVersionTest,
    "NarrRail.Save.SessionSnapshot.RejectUnsupportedVersion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNarrRailSessionSnapshotRejectUnsupportedVersionTest::RunTest(const FString& Parameters)
{
    using namespace NarrRailSaveSnapshotTests;

    UNarrRailStoryAsset* Story = MakeStory(TEXT("NR_SaveSnapshot_VersionGate"), TEXT("VersionGate"));

    UNarrRailStorySession* SourceSession = MakeSessionAtPlain(*this, Story, TEXT("Source"));
    if (SourceSession == nullptr)
    {
        return false;
    }

    const FNarrRailStorySessionSnapshot ValidSnapshot = SourceSession->GetSessionSnapshot();

    // T020 更新 / T021 更旧 / T022 缺失或畸形。
    //
    // 注意这里「更旧」与「缺失」在当前布局下必然重叠：只有版本 1 存在过，所以没有任何正整数比它
    // 更旧，而字段缺失反序列化后拿到的就是默认值 0。这不是测试的疏漏，是真实的形状，如实标注。
    struct FVersionCase
    {
        const TCHAR* Label;
        int32 Version;
    };

    const FVersionCase Cases[] =
    {
        { TEXT("T020 newer than supported"), 2 },
        { TEXT("T021 older than supported / absent (default)"), 0 },
        { TEXT("T022 malformed (negative)"), -1 },
        { TEXT("T022 malformed (far future)"), MAX_int32 },
    };

    for (const FVersionCase& Case : Cases)
    {
        FNarrRailStorySessionSnapshot Snapshot = ValidSnapshot;
        Snapshot.SnapshotVersion = Case.Version;

        UNarrRailStorySession* RestoredSession = MakeSessionAtPlain(*this, Story, TEXT("Restored"));
        if (RestoredSession == nullptr)
        {
            return false;
        }

        // 把目标会话推到一个与快照不同的状态。否则「没被改动」是一句无法证伪的话：本来就和快照
        // 一样，就算版本门失灵、恢复被错误接受，状态看上去也不会变。
        TestEqual(TEXT("Restored target advances away from the snapshot"), RestoredSession->Next().Code, ENarrRailRuntimeResultCode::Success);
        TestTrue(TEXT("Restored target changes a variable away from the snapshot"), RestoredSession->SetVariableInt(TEXT("Score"), 99).IsSuccess());
        TestTrue(TEXT("Restored target is not where the snapshot points"), RestoredSession->GetCurrentNodeId() != FName(PlainNodeId));

        const FObservableSessionState Before = FObservableSessionState::Capture(RestoredSession);

        const FNarrRailRuntimeResult RestoreResult = RestoredSession->RestoreSessionSnapshot(Snapshot, false);
        TestEqual(FString::Printf(TEXT("%s is rejected"), Case.Label), RestoreResult.Code, ENarrRailRuntimeResultCode::InvalidInput);

        AssertSessionUnchanged(*this, Case.Label, Before, RestoredSession);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrRailSessionSnapshotRejectMissingRequiredFieldTest,
    "NarrRail.Save.SessionSnapshot.RejectMissingRequiredField",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNarrRailSessionSnapshotRejectMissingRequiredFieldTest::RunTest(const FString& Parameters)
{
    using namespace NarrRailSaveSnapshotTests;

    UNarrRailStoryAsset* Story = MakeStory(TEXT("NR_SaveSnapshot_MissingField"), TEXT("MissingField"));

    UNarrRailStorySession* RestoredSession = MakeSessionAtPlain(*this, Story, TEXT("Restored"));
    if (RestoredSession == nullptr)
    {
        return false;
    }

    const FObservableSessionState Before = FObservableSessionState::Capture(RestoredSession);

    // 一个从没被采集过的快照：所有字段都是默认值，等价于「旧存档里这些字段根本不存在」。
    // 这正是 SnapshotVersion 默认 0 而不是 1 想拦住的情况——默认 1 的话它会静默通过版本门。
    const FNarrRailStorySessionSnapshot EmptySnapshot;

    const FNarrRailRuntimeResult RestoreResult = RestoredSession->RestoreSessionSnapshot(EmptySnapshot, false);
    TestEqual(TEXT("Snapshot with no version is rejected"), RestoreResult.Code, ENarrRailRuntimeResultCode::InvalidInput);

    AssertSessionUnchanged(*this, TEXT("Rejected snapshot"), Before, RestoredSession);

    return true;
}

// ============================================================================
// T023 - 引用的节点无法解析时，失败信息点名那个节点
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrRailSessionSnapshotRejectUnresolvableNodeTest,
    "NarrRail.Save.SessionSnapshot.RejectUnresolvableNode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNarrRailSessionSnapshotRejectUnresolvableNodeTest::RunTest(const FString& Parameters)
{
    using namespace NarrRailSaveSnapshotTests;

    UNarrRailStoryAsset* Story = MakeStory(TEXT("NR_SaveSnapshot_MissingNode"), TEXT("MissingNode"));

    UNarrRailStorySession* SourceSession = MakeSessionAtPlain(*this, Story, TEXT("Source"));
    if (SourceSession == nullptr)
    {
        return false;
    }

    FNarrRailStorySessionSnapshot Snapshot = SourceSession->GetSessionSnapshot();
    Snapshot.CurrentNodeId = FName(TEXT("Ghost"));

    UNarrRailStorySession* RestoredSession = MakeSessionAtPlain(*this, Story, TEXT("Restored"));
    if (RestoredSession == nullptr)
    {
        return false;
    }

    const FObservableSessionState Before = FObservableSessionState::Capture(RestoredSession);

    const FNarrRailRuntimeResult RestoreResult = RestoredSession->RestoreSessionSnapshot(Snapshot, false);
    TestEqual(TEXT("Unresolvable node is rejected"), RestoreResult.Code, ENarrRailRuntimeResultCode::MissingNode);
    TestEqual(TEXT("Failure names the offending node"), RestoreResult.NodeId, FName(TEXT("Ghost")));

    AssertSessionUnchanged(*this, TEXT("Unresolvable-node rejection"), Before, RestoredSession);

    return true;
}

// ============================================================================
// T032 - FR-009 一致性门
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrRailSessionSnapshotConsistencyGateTest,
    "NarrRail.Save.SessionSnapshot.ConsistencyGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNarrRailSessionSnapshotConsistencyGateTest::RunTest(const FString& Parameters)
{
    using namespace NarrRailSaveSnapshotTests;

    // 选项数是 3，用来构造「已消费索引越界」。
    UNarrRailStoryAsset* Story = MakeStory(TEXT("NR_SaveSnapshot_ConsistencyGate"), TEXT("ConsistencyGate"), 3);

    // --- 基线快照：停在 Branch0，已消费记录与返回栈都指向 Pick ---
    UNarrRailStorySession* SourceSession = NewObject<UNarrRailStorySession>(GetTransientPackage());
    TestEqual(TEXT("Source Initialize succeeds"), SourceSession->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);
    TestEqual(TEXT("Source Start succeeds"), SourceSession->Start().Code, ENarrRailRuntimeResultCode::Success);
    TestTrue(TEXT("Source walks to the choice node"), PlayToNode(*this, SourceSession, FName(ChoiceNodeId)));
    TestEqual(TEXT("Source picks the first option"), SourceSession->Choose(0).Code, ENarrRailRuntimeResultCode::Success);

    const FNarrRailStorySessionSnapshot BaseSnapshot = SourceSession->GetSessionSnapshot();

    // --- 用例 1：行索引超出当前节点的行数范围 ---
    {
        UNarrRailStorySession* Target = NewObject<UNarrRailStorySession>(GetTransientPackage());
        TestEqual(TEXT("[LineRange] Initialize succeeds"), Target->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);
        TestEqual(TEXT("[LineRange] Start succeeds"), Target->Start().Code, ENarrRailRuntimeResultCode::Success);

        // 把会话停在 Intro（MultiDialogue，2 行）再改快照，这样当前节点确实有一条可越界的行索引。
        FNarrRailStorySessionSnapshot Snapshot = Target->GetSessionSnapshot();
        Snapshot.CurrentMultiDialogueLineIndex = 99;

        const FObservableSessionState Before = FObservableSessionState::Capture(Target);
        const FNarrRailRuntimeResult Result = Target->RestoreSessionSnapshot(Snapshot, false);
        TestEqual(TEXT("[LineRange] Out-of-range line index is rejected"), Result.Code, ENarrRailRuntimeResultCode::InvalidInput);

        AssertSessionUnchanged(*this, TEXT("[LineRange] Rejection"), Before, Target);
    }

    // --- 用例 2：行索引有值，但当前节点根本不是 MultiDialogue ---
    {
        UNarrRailStorySession* Target = NewObject<UNarrRailStorySession>(GetTransientPackage());
        TestEqual(TEXT("[LineOnNonMD] Initialize succeeds"), Target->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);
        TestEqual(TEXT("[LineOnNonMD] Start succeeds"), Target->Start().Code, ENarrRailRuntimeResultCode::Success);
        TestTrue(TEXT("[LineOnNonMD] walks to the plain node"), PlayToNode(*this, Target, FName(PlainNodeId)));

        FNarrRailStorySessionSnapshot Snapshot = Target->GetSessionSnapshot();
        Snapshot.CurrentMultiDialogueLineIndex = 0;

        const FObservableSessionState Before = FObservableSessionState::Capture(Target);
        const FNarrRailRuntimeResult Result = Target->RestoreSessionSnapshot(Snapshot, false);
        TestEqual(TEXT("[LineOnNonMD] Line index on a non-multi-dialogue node is rejected"), Result.Code, ENarrRailRuntimeResultCode::InvalidInput);

        AssertSessionUnchanged(*this, TEXT("[LineOnNonMD] Rejection"), Before, Target);
    }

    // --- 用例 3：已消费记录指向一个已被删除的节点 ---
    {
        FNarrRailStorySessionSnapshot Snapshot = BaseSnapshot;
        FNarrRailChoiceSelectionSnapshot GhostRecord;
        GhostRecord.ChoiceNodeId = FName(TEXT("Ghost"));
        GhostRecord.SelectedChoiceIndices.Add(0);
        Snapshot.ExhaustiveChoiceSelections.Add(GhostRecord);

        UNarrRailStorySession* Target = NewObject<UNarrRailStorySession>(GetTransientPackage());
        TestEqual(TEXT("[DeletedChoiceNode] Initialize succeeds"), Target->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);

        const FObservableSessionState Before = FObservableSessionState::Capture(Target);
        const FNarrRailRuntimeResult Result = Target->RestoreSessionSnapshot(Snapshot, false);
        TestEqual(TEXT("[DeletedChoiceNode] Record for a deleted node is rejected"), Result.Code, ENarrRailRuntimeResultCode::InvalidInput);
        TestEqual(TEXT("[DeletedChoiceNode] Failure names the offending node"), Result.NodeId, FName(TEXT("Ghost")));

        AssertSessionUnchanged(*this, TEXT("[DeletedChoiceNode] Rejection"), Before, Target);
    }

    // --- 用例 4：已消费选项索引超出该节点的选项数 ---
    {
        FNarrRailStorySessionSnapshot Snapshot = BaseSnapshot;
        TestEqual(TEXT("[OptionIndex] Baseline has one record"), Snapshot.ExhaustiveChoiceSelections.Num(), 1);
        Snapshot.ExhaustiveChoiceSelections[0].SelectedChoiceIndices.Add(99);

        UNarrRailStorySession* Target = NewObject<UNarrRailStorySession>(GetTransientPackage());
        TestEqual(TEXT("[OptionIndex] Initialize succeeds"), Target->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);

        const FObservableSessionState Before = FObservableSessionState::Capture(Target);
        const FNarrRailRuntimeResult Result = Target->RestoreSessionSnapshot(Snapshot, false);
        TestEqual(TEXT("[OptionIndex] Out-of-range consumed index is rejected"), Result.Code, ENarrRailRuntimeResultCode::InvalidInput);
        TestEqual(TEXT("[OptionIndex] Failure names the offending node"), Result.NodeId, FName(ChoiceNodeId));

        AssertSessionUnchanged(*this, TEXT("[OptionIndex] Rejection"), Before, Target);
    }

    // --- 用例 5：返回栈上有一个已被删除的节点 ---
    //
    // 这一条是 T001 枚举参考实现时补上的。栈里的悬空 Id 不会被恢复发现，而是等到该分支结束、
    // TryPopExhaustiveReturn 把它交给 AdvanceToNode 时才以 MissingNode 失败——那时上下文早已
    // 远离成因。这里断言它现在就在恢复阶段被拦下。
    {
        FNarrRailStorySessionSnapshot Snapshot = BaseSnapshot;
        Snapshot.ExhaustivePendingChoiceReturnStack.Reset();
        Snapshot.ExhaustivePendingChoiceReturnStack.Add(FName(TEXT("Ghost")));

        UNarrRailStorySession* Target = NewObject<UNarrRailStorySession>(GetTransientPackage());
        TestEqual(TEXT("[ReturnStack] Initialize succeeds"), Target->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);

        const FObservableSessionState Before = FObservableSessionState::Capture(Target);
        const FNarrRailRuntimeResult Result = Target->RestoreSessionSnapshot(Snapshot, false);
        TestEqual(TEXT("[ReturnStack] Deleted node on the return stack is rejected"), Result.Code, ENarrRailRuntimeResultCode::MissingNode);
        TestEqual(TEXT("[ReturnStack] Failure names the offending node"), Result.NodeId, FName(TEXT("Ghost")));

        AssertSessionUnchanged(*this, TEXT("[ReturnStack] Rejection"), Before, Target);
    }

    // --- 用例 6（反向断言）：NodeHistory 里的悬空 Id 必须被接受 ---
    //
    // 这一条防的是门越界。NodeHistory 只被记录、从不被运行时解引用，而且是只增不减的：作者删掉
    // 一个曾访问过的节点之后，它必然留下一个悬空 Id。如果哪天有人「顺手」把历史也纳入校验，普通
    // 编辑之后的存档就会全部读不进去，而这里会立刻变红。
    {
        FNarrRailStorySessionSnapshot Snapshot = BaseSnapshot;
        Snapshot.NodeHistory.Add(FName(TEXT("Ghost")));

        UNarrRailStorySession* Target = NewObject<UNarrRailStorySession>(GetTransientPackage());
        TestEqual(TEXT("[HistoryNotGated] Initialize succeeds"), Target->Initialize(Story).Code, ENarrRailRuntimeResultCode::Success);

        const FNarrRailRuntimeResult Result = Target->RestoreSessionSnapshot(Snapshot, false);
        TestEqual(TEXT("[HistoryNotGated] Dangling id in NodeHistory is accepted"), Result.Code, ENarrRailRuntimeResultCode::Success);
        TestEqual(TEXT("[HistoryNotGated] Restored onto the captured node"), Target->GetCurrentNodeId(), FName(TEXT("Branch0")));
    }

    return true;
}

// ============================================================================
// 全局状态快照
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrRailGlobalStateSnapshotRestoreTest,
    "NarrRail.Save.GlobalStateSnapshot.RestoresPresetSpeakersAndValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNarrRailGlobalStateSnapshotRestoreTest::RunTest(const FString& Parameters)
{
    using namespace NarrRailSaveSnapshotTests;

    UNarrRailGlobalConfigAsset* Config = MakeGlobalConfig(TEXT("/NarrRailAutomation/SaveSnapshotGlobalConfig"), TEXT("SaveSnapshotGlobalConfig"));

    UGameInstance* SourceGameInstance = NewObject<UGameInstance>(GetTransientPackage());
    UNarrRailGlobalStateSubsystem* SourceState = NewObject<UNarrRailGlobalStateSubsystem>(SourceGameInstance);
    FString ErrorMessage;
    TestTrue(TEXT("ApplyGlobalConfig succeeds"), SourceState->ApplyGlobalConfig(Config, ErrorMessage));
    TestNotNull(TEXT("Source global variables exist"), SourceState->GetGlobalVariableContainer());
    TestTrue(TEXT("Source Trust write succeeds"), SourceState->GetGlobalVariableContainer()->SetInt(TEXT("Trust"), 42).IsSuccess());

    const FNarrRailGlobalStateSnapshot Snapshot = SourceState->GetGlobalStateSnapshot();
    TestTrue(TEXT("Snapshot records the applied global config path"), Snapshot.AppliedGlobalConfigPaths.Num() > 0);
    TestEqual(TEXT("Global snapshot stamps a supported version"), Snapshot.SnapshotVersion, 1);

    UGameInstance* RestoredGameInstance = NewObject<UGameInstance>(GetTransientPackage());
    UNarrRailGlobalStateSubsystem* RestoredState = NewObject<UNarrRailGlobalStateSubsystem>(RestoredGameInstance);
    ErrorMessage.Reset();
    const bool bGlobalRestoreSucceeded = RestoredState->RestoreGlobalStateSnapshot(Snapshot, ErrorMessage);
    TestTrue(FString::Printf(TEXT("RestoreGlobalStateSnapshot succeeds (%s)"), *ErrorMessage), bGlobalRestoreSucceeded);

    FNarrRailPresetSpeaker RestoredSpeaker;
    TestTrue(TEXT("Preset speaker restored"), RestoredState->GetPresetSpeaker(TEXT("Alice"), RestoredSpeaker));
    TestEqual(TEXT("Preset speaker display name restored"), RestoredSpeaker.DisplayName, FString(TEXT("Alice Display")));

    int32 Trust = 0;
    TestNotNull(TEXT("Restored global variables exist"), RestoredState->GetGlobalVariableContainer());
    TestTrue(TEXT("Restored Trust can be read"), RestoredState->GetGlobalVariableContainer()->GetInt(TEXT("Trust"), Trust).IsSuccess());
    TestEqual(TEXT("Global variable value restored"), Trust, 42);

    return true;
}

// 全局快照的版本门：不只是非正数，任何与受支持版本不等的值都必须被拒。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNarrRailGlobalStateSnapshotRejectUnsupportedVersionTest,
    "NarrRail.Save.GlobalStateSnapshot.RejectUnsupportedVersion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNarrRailGlobalStateSnapshotRejectUnsupportedVersionTest::RunTest(const FString& Parameters)
{
    using namespace NarrRailSaveSnapshotTests;

    UNarrRailGlobalConfigAsset* Config = MakeGlobalConfig(TEXT("/NarrRailAutomation/SaveSnapshotGlobalConfigVersion"), TEXT("SaveSnapshotGlobalConfigVersion"));

    UGameInstance* SourceGameInstance = NewObject<UGameInstance>(GetTransientPackage());
    UNarrRailGlobalStateSubsystem* SourceState = NewObject<UNarrRailGlobalStateSubsystem>(SourceGameInstance);
    FString ErrorMessage;
    TestTrue(TEXT("ApplyGlobalConfig succeeds"), SourceState->ApplyGlobalConfig(Config, ErrorMessage));
    TestTrue(TEXT("Source Trust write succeeds"), SourceState->GetGlobalVariableContainer()->SetInt(TEXT("Trust"), 42).IsSuccess());

    const FNarrRailGlobalStateSnapshot ValidSnapshot = SourceState->GetGlobalStateSnapshot();

    const int32 UnsupportedVersions[] = { 0, 2, -1 };

    for (const int32 Version : UnsupportedVersions)
    {
        FNarrRailGlobalStateSnapshot Snapshot = ValidSnapshot;
        Snapshot.SnapshotVersion = Version;

        UGameInstance* TargetGameInstance = NewObject<UGameInstance>(GetTransientPackage());
        UNarrRailGlobalStateSubsystem* TargetState = NewObject<UNarrRailGlobalStateSubsystem>(TargetGameInstance);

        FString TargetError;
        TestFalse(FString::Printf(TEXT("Version %d is rejected"), Version),
            TargetState->RestoreGlobalStateSnapshot(Snapshot, TargetError));
        TestTrue(FString::Printf(TEXT("Version %d reports a reason"), Version), !TargetError.IsEmpty());

        // 参考实现只判 <= 0，那样版本 2 会被当成合法快照照单全收。受支持版本被显式拒绝才是本用例的重点。
        TestNull(FString::Printf(TEXT("Version %d leaves globals unset"), Version), TargetState->GetGlobalVariableContainer());
    }

    return true;
}

#endif
