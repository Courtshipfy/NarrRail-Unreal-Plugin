#pragma once

#include "CoreMinimal.h"
#include "Runtime/NarrRailGlobalConfigAsset.h"
#include "Runtime/NarrRailGlobalStateSubsystem.h"
#include "Runtime/NarrRailStoryAsset.h"
#include "Runtime/NarrRailVariableContainer.h"
#include "Runtime/NarrRailDialoguePresenterInterface.h"
#include "NarrRailStorySession.generated.h"

UENUM(BlueprintType)
enum class ENarrRailRuntimeResultCode : uint8
{
    Success UMETA(DisplayName = "Success"),
    InvalidState UMETA(DisplayName = "Invalid State"),
    InvalidInput UMETA(DisplayName = "Invalid Input"),
    MissingNode UMETA(DisplayName = "Missing Node"),
    Completed UMETA(DisplayName = "Completed")
};

UENUM(BlueprintType)
enum class ENarrRailSessionState : uint8
{
    Idle UMETA(DisplayName = "Idle"),
    Running UMETA(DisplayName = "Running"),
    WaitingForChoice UMETA(DisplayName = "Waiting For Choice"),
    Paused UMETA(DisplayName = "Paused"),
    Completed UMETA(DisplayName = "Completed"),
    Error UMETA(DisplayName = "Error")
};

// 会话上下文：单次剧情会话的运行时快照，保证会话状态彼此隔离。
USTRUCT(BlueprintType)
struct NARRRAIL_API FNarrRailSessionContext
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NarrRail")
    FName CurrentNodeId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NarrRail")
    TArray<FName> NodeHistory;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NarrRail")
    TMap<FName, FString> VariableSnapshot;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NarrRail")
    TArray<FName> EmittedEvents;
};

USTRUCT(BlueprintType)
struct NARRRAIL_API FNarrRailRuntimeResult
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NarrRail")
    ENarrRailRuntimeResultCode Code = ENarrRailRuntimeResultCode::Success;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NarrRail")
    FString Message;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NarrRail")
    FName NodeId = NAME_None;

    static FNarrRailRuntimeResult Make(const ENarrRailRuntimeResultCode InCode, const TCHAR* InMessage, const FName InNodeId = NAME_None)
    {
        FNarrRailRuntimeResult Result;
        Result.Code = InCode;
        Result.Message = InMessage;
        Result.NodeId = InNodeId;
        return Result;
    }
};

// 存档快照内单个 Choice 节点的已消费选项记录。
//
// 每个被部分消费的 Choice 节点一条记录，而不是每个已消费选项一条：选项分散成 N 条会丢失
// 「它们属于同一节点」这一事实，恢复时也就无法校验选项索引是否落在该节点的选项数范围内。
USTRUCT(BlueprintType)
struct NARRRAIL_API FNarrRailChoiceSelectionSnapshot
{
    GENERATED_BODY()

    // 该记录对应的 Choice 节点 Id。NAME_None 的记录在恢复时被忽略。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    FName ChoiceNodeId = NAME_None;

    // 该节点上已消费的选项的原始索引，采集时升序排序。
    //
    // 排序是刻意的：内存中的 TSet 没有稳定的迭代顺序，直接序列化会让「同一状态」产出两个
    // 不相等的快照，而「采集不改变状态」的断言正是靠快照可比来成立的。恢复时负索引被忽略。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    TArray<int32> SelectedChoiceIndices;
};

// 单次剧情会话的完整可恢复状态。由会话产出，由会话消费。
//
// 这不是中立格式的产物：它永远不会出现在 .nrstory / GlobalConfig / .nroutline 文件里，
// 因此它的版本号也与 .nrstory 的 schemaVersion 无关（见 specs/0001 的 data-model.md）。
//
// 暴露面（FR-007）：结构体本身必须是 BlueprintType，因为它出现在下面两个 UFUNCTION 的签名里；
// 但字段一个都不开放 Blueprint 读写。参考实现把 14 个字段全部标成 BlueprintReadWrite，那比
// FR-007 允许的宽——蓝图侧可以自己拼一个快照出来，甚至改写 SnapshotVersion，而这正是版本门和
// 一致性门要拦的事情。快照只应该由采集产出、由恢复消费，中间不经过蓝图拼装。
USTRUCT(BlueprintType)
struct NARRRAIL_API FNarrRailStorySessionSnapshot
{
    GENERATED_BODY()

    // 快照布局版本，用于版本门。
    //
    // 默认值刻意是 0 而不是 1。参考实现默认 1，于是「字段缺失」与「显式声明为 1」在反序列化后
    // 无法区分，FR-003 要求的「缺失即拒绝」根本无从实现：旧存档缺这个字段时会被静默当成当前
    // 版本接受。默认 0 让缺失变成一个必然被拒的非法值，采集时再显式盖章为受支持版本。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    int32 SnapshotVersion = 0;

    // 身份校验：与已加载资产自身的 StoryId 比对。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    FName StoryId = NAME_None;

    // 身份校验：与当前 StoryAsset 路径比对。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    FSoftObjectPath StoryAssetPath;

    // 身份校验：与当前 GlobalConfig 路径比对。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    FSoftObjectPath GlobalConfigPath;

    // 会话生命周期状态。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    ENarrRailSessionState SessionState = ENarrRailSessionState::Idle;

    // 暂停期间恢复时应当回到的状态。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    ENarrRailSessionState StateBeforePause = ENarrRailSessionState::Idle;

    // 采集时会话所在的节点。稳定的 FName Id，不是位置索引——位置索引会在作者编辑图之后失效。
    // 一致性门会校验它仍能解析。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    FName CurrentNodeId = NAME_None;

    // 访问过的节点轨迹。
    //
    // 刻意不纳入一致性门，理由见 specs/0001 的 data-model.md invariant 6：运行时从不解析这个
    // 列表，只把它原样交给调用方，而且它只增不减，因此在作者删掉一个曾访问过的节点之后，这里
    // 必然留下一个悬空 Id。对它设门等于在普通编辑之后就拒绝存档，恰恰是 FR-009 要避免的。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    TArray<FName> NodeHistory;

    // 已触发过的事件 Id，避免恢复后重复触发。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    TArray<FName> EmittedEvents;

    // 会话局部变量。全局变量不在这里，它们在 UNarrRailGlobalStateSubsystem 自己的快照里。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    TMap<FName, FString> LocalVariableSnapshot;

    // 在 MultiDialogue 节点内推进到第几行；哨兵值 INDEX_NONE 表示不在多行对话中。
    // 一致性门会校验它落在节点行数范围内或等于哨兵。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    int32 CurrentMultiDialogueLineIndex = INDEX_NONE;

    // 最后一次选择的信息，分支逻辑和 presenter 都会读它。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    FNarrRailLastChoiceInfo LastChoiceInfo;

    // 每个被部分消费的 Choice 节点的已消费选项，是 ExhaustiveSelectedChoiceIndices 的落盘形式。
    // 之所以要有这个中转：UHT 不支持 TMap 的 value 是容器类型，那个 TSet 无法挂 UPROPERTY，
    // 也就无法序列化。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    TArray<FNarrRailChoiceSelectionSnapshot> ExhaustiveChoiceSelections;

    // 穷举选择分支的待返回栈，支持嵌套。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    TArray<FName> ExhaustivePendingChoiceReturnStack;
};

// 运行时事件委托声明
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNarrRailSessionStartedDelegate, FName, EntryNodeId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNarrRailNodeEnteredDelegate, FName, NodeId, const FNarrRailNode&, Node);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNarrRailNodeExitedDelegate, FName, NodeId, const FNarrRailNode&, Node);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNarrRailSessionEndedDelegate, FName, LastNodeId, ENarrRailSessionState, EndState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNarrRailChoicesReadyDelegate, FName, NodeId, const TArray<FNarrRailChoiceOption>&, Choices);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNarrRailChoiceSelectedDelegate, FName, NodeId, int32, ChoiceIndex, FName, TargetNodeId);

// 剧情会话执行器：负责运行时 Start/Next/Choose/Pause/Resume/Stop 流程推进。
UCLASS(BlueprintType)
class NARRRAIL_API UNarrRailStorySession : public UObject
{
    GENERATED_BODY()

public:
    // === 生命周期管理 ===

    virtual void BeginDestroy() override;

    // === 会话管理接口 ===

    UFUNCTION(BlueprintCallable, Category = "NarrRail|Runtime")
    FNarrRailRuntimeResult Initialize(const UNarrRailStoryAsset* InStoryAsset);

    UFUNCTION(BlueprintCallable, Category = "NarrRail|Runtime")
    FNarrRailRuntimeResult InitializeWithGlobalConfig(const UNarrRailStoryAsset* InStoryAsset, const UNarrRailGlobalConfigAsset* InGlobalConfig);

    UFUNCTION(BlueprintCallable, Category = "NarrRail|Runtime")
    FNarrRailRuntimeResult Start(FName OverrideEntryNodeId = NAME_None);

    UFUNCTION(BlueprintCallable, Category = "NarrRail|Runtime")
    FNarrRailRuntimeResult Next();

    UFUNCTION(BlueprintCallable, Category = "NarrRail|Runtime")
    FNarrRailRuntimeResult Choose(int32 ChoiceIndex);

    UFUNCTION(BlueprintCallable, Category = "NarrRail|Runtime")
    FNarrRailRuntimeResult Pause();

    UFUNCTION(BlueprintCallable, Category = "NarrRail|Runtime")
    FNarrRailRuntimeResult Resume();

    UFUNCTION(BlueprintCallable, Category = "NarrRail|Runtime")
    FNarrRailRuntimeResult Stop();

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    ENarrRailSessionState GetSessionState() const
    {
        return SessionState;
    }

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    FName GetCurrentNodeId() const
    {
        return Context.CurrentNodeId;
    }

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    int32 GetCurrentMultiDialogueLineIndex() const
    {
        return CurrentMultiDialogueLineIndex;
    }

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    int32 GetCurrentMultiDialogueTotalLines() const;

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    bool IsCurrentNodeMultiDialogue() const;

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    bool GetCurrentNode(FNarrRailNode& OutNode) const;

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    TArray<FNarrRailChoiceOption> GetCurrentChoices() const;

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    const TArray<FName>& GetHistory() const
    {
        return Context.NodeHistory;
    }

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    FNarrRailSessionContext GetSessionContext() const
    {
        return Context;
    }

    // 获取最后一次选择的信息
    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    FNarrRailLastChoiceInfo GetLastChoice() const
    {
        return LastChoiceInfo;
    }

    // 获取变量容器（用于直接访问变量系统）
    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    UNarrRailVariableContainer* GetVariableContainer() const
    {
        return VariableContainer;
    }

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    UNarrRailVariableContainer* GetGlobalVariableContainer() const
    {
        return GlobalVariableContainer;
    }

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    const UNarrRailGlobalConfigAsset* GetGlobalConfigAsset() const
    {
        return GlobalConfigAsset;
    }

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    bool GetPresetSpeaker(FName SpeakerId, FNarrRailPresetSpeaker& OutSpeaker) const;

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    FString ResolveSpeakerDisplayName(FName SpeakerId) const;

    // === 存档快照（FR-007 允许暴露的全部接口）===

    /**
     * 采集当前会话的可恢复状态。
     *
     * 这是一个不改变会话状态的纯操作：采集之后继续游玩，与从未采集过完全一致（FR-006）。
     */
    UFUNCTION(BlueprintPure, Category = "NarrRail|Save")
    FNarrRailStorySessionSnapshot GetSessionSnapshot() const;

    /**
     * 用快照恢复会话状态。
     *
     * 版本门、身份校验、一致性门全部在任何写入之前完成；任一失败都直接返回错误且会话状态原样
     * 不动（FR-008）。成功时 Out 的 presenter 状态由 bRefreshPresenter 决定，而不是继承一个
     * 进行中的动画状态（FR-011）。
     *
     * @param Snapshot          要恢复的快照，通常来自 GetSessionSnapshot()。
     * @param bRefreshPresenter 是否在恢复后重新驱动 presenter。
     */
    UFUNCTION(BlueprintCallable, Category = "NarrRail|Save")
    FNarrRailRuntimeResult RestoreSessionSnapshot(const FNarrRailStorySessionSnapshot& Snapshot, bool bRefreshPresenter = true);


    // 便捷的变量访问接口
    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    FNarrRailVariableResult GetVariableBool(FName VariableName, bool& OutValue) const;

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    FNarrRailVariableResult GetVariableInt(FName VariableName, int32& OutValue) const;

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    FNarrRailVariableResult GetVariableFloat(FName VariableName, float& OutValue) const;

    UFUNCTION(BlueprintPure, Category = "NarrRail|Runtime")
    FNarrRailVariableResult GetVariableString(FName VariableName, FString& OutValue) const;

    UFUNCTION(BlueprintCallable, Category = "NarrRail|Runtime")
    FNarrRailVariableResult SetVariableBool(FName VariableName, bool Value);

    UFUNCTION(BlueprintCallable, Category = "NarrRail|Runtime")
    FNarrRailVariableResult SetVariableInt(FName VariableName, int32 Value);

    UFUNCTION(BlueprintCallable, Category = "NarrRail|Runtime")
    FNarrRailVariableResult SetVariableFloat(FName VariableName, float Value);

    UFUNCTION(BlueprintCallable, Category = "NarrRail|Runtime")
    FNarrRailVariableResult SetVariableString(FName VariableName, const FString& Value);

    // === UI 显示器管理 ===

    /**
     * 注册对话显示器（UI 组件）
     * @param Presenter 实现了 INarrRailDialoguePresenterInterface 的 UI 组件
     */
    UFUNCTION(BlueprintCallable, Category = "NarrRail|UI")
    void RegisterDialoguePresenter(TScriptInterface<INarrRailDialoguePresenterInterface> Presenter);

    /**
     * 取消注册对话显示器
     */
    UFUNCTION(BlueprintCallable, Category = "NarrRail|UI")
    void UnregisterDialoguePresenter();

    /**
     * 获取当前注册的对话显示器
     * @return 当前的对话显示器，如果未注册则返回 nullptr
     */
    UFUNCTION(BlueprintPure, Category = "NarrRail|UI")
    TScriptInterface<INarrRailDialoguePresenterInterface> GetDialoguePresenter() const
    {
        return DialoguePresenter;
    }

    // === 运行时事件委托 ===

    // 会话启动事件
    UPROPERTY(BlueprintAssignable, Category = "NarrRail|Events")
    FNarrRailSessionStartedDelegate OnSessionStarted;

    // 节点进入事件
    UPROPERTY(BlueprintAssignable, Category = "NarrRail|Events")
    FNarrRailNodeEnteredDelegate OnNodeEntered;

    // 节点退出事件
    UPROPERTY(BlueprintAssignable, Category = "NarrRail|Events")
    FNarrRailNodeExitedDelegate OnNodeExited;

    // 会话结束事件
    UPROPERTY(BlueprintAssignable, Category = "NarrRail|Events")
    FNarrRailSessionEndedDelegate OnSessionEnded;

    // 选项准备就绪事件
    UPROPERTY(BlueprintAssignable, Category = "NarrRail|Events")
    FNarrRailChoicesReadyDelegate OnChoicesReady;

    // 选项被选择事件
    UPROPERTY(BlueprintAssignable, Category = "NarrRail|Events")
    FNarrRailChoiceSelectedDelegate OnChoiceSelected;

    // === 全局会话管理（用于调试器自动查找） ===

    /**
     * 获取所有活跃的会话
     * @return 当前所有活跃的会话列表
     */
    static TArray<UNarrRailStorySession*> GetAllActiveSessions();

    /**
     * 设置会话的调试名称（可选，用于多会话场景）
     * @param InDebugName 调试名称
     */
    UFUNCTION(BlueprintCallable, Category = "NarrRail|Debug")
    void SetDebugName(const FString& InDebugName);

    /**
     * 获取会话的调试名称
     * @return 调试名称
     */
    UFUNCTION(BlueprintPure, Category = "NarrRail|Debug")
    FString GetDebugName() const { return DebugName; }

private:
    void ResetSessionContextFromAsset();
    void SyncVariableSnapshotToContext();
    bool BuildMultiDialogueDisplay(const FNarrRailNode& Node, FNarrRailDialogueRequest& OutRequest) const;
    FString MakeDefaultVariableValue(ENarrRailVariableType VariableType) const;
    FNarrRailRuntimeResult AdvanceToNode(FName TargetNodeId);
    FNarrRailRuntimeResult ResolveNextByEdge(const FNarrRailNode& FromNode, FName& OutNextNodeId) const;
    const FNarrRailNode* FindNode(FName NodeId) const;

    bool EvaluateConditionExpression(const FNarrRailConditionExpression& Condition) const;
    bool EvaluateConditionBranch(const FNarrRailConditionBranch& Branch) const;
    bool EvaluateConditionTerm(const FNarrRailConditionTerm& Term) const;
    bool ExecuteActions(const TArray<FNarrRailNodeAction>& Actions, FString& OutErrorMessage);
    UNarrRailVariableContainer* FindVariableContainerForName(FName VariableName) const;
    // 恢复之后重新驱动 presenter，使其状态由恢复结果决定而非继承进行中的动画（FR-011）。
    void RefreshCurrentNodeAfterRestore();

    // === 恢复路径上的三道门（FR-003 / FR-009 / FR-010）===
    //
    // 三个函数都只做校验、不写任何状态，因此调用方可以先把它们全部跑完再开始提交，从而保证
    // 任一失败都留下一个未被触碰的会话（FR-008）。返回 Success 之外的任何 Code 都应当被直接
    // 上抛给调用者。
    FNarrRailRuntimeResult ValidateSnapshotVersion(const FNarrRailStorySessionSnapshot& Snapshot) const;
    FNarrRailRuntimeResult ValidateSnapshotIdentity(const FNarrRailStorySessionSnapshot& Snapshot) const;
    FNarrRailRuntimeResult ValidateSnapshotConsistency(const FNarrRailStorySessionSnapshot& Snapshot) const;

    TArray<int32> BuildAvailableChoiceIndices(const FNarrRailNode& ChoiceNode) const;
    TArray<int32> BuildVisibleChoiceIndices(const FNarrRailNode& ChoiceNode) const;
    TArray<FNarrRailChoiceOption> BuildVisibleChoiceOptions(const FNarrRailNode& ChoiceNode) const;

    // 穷举选择分支结束时的统一回返处理
    bool TryPopExhaustiveReturn(FName& OutReturnNodeId);

private:
    UPROPERTY(Transient)
    TObjectPtr<const UNarrRailStoryAsset> StoryAsset;

    UPROPERTY(Transient)
    TObjectPtr<const UNarrRailGlobalConfigAsset> GlobalConfigAsset;

    UPROPERTY(Transient)
    ENarrRailSessionState SessionState = ENarrRailSessionState::Idle;

    UPROPERTY(Transient)
    ENarrRailSessionState StateBeforePause = ENarrRailSessionState::Idle;

    UPROPERTY(Transient)
    FNarrRailSessionContext Context;

    UPROPERTY(Transient)
    TObjectPtr<UNarrRailVariableContainer> VariableContainer;

    UPROPERTY(Transient)
    TObjectPtr<UNarrRailVariableContainer> GlobalVariableContainer;

    UPROPERTY(Transient)
    FNarrRailLastChoiceInfo LastChoiceInfo;

    UPROPERTY(Transient)
    int32 CurrentMultiDialogueLineIndex = INDEX_NONE;

    // 运行时缓存：UHT 不支持 TMap value 为容器类型，因此不使用 UPROPERTY
    TMap<FName, TSet<int32>> ExhaustiveSelectedChoiceIndices;

    // 运行时缓存：Choice 可见索引映射（UI index -> 原始 choice index）
    TMap<FName, TArray<int32>> RuntimeVisibleChoiceIndexMap;

    // 穷举选择分支的待返回栈（支持嵌套）
    TArray<FName> ExhaustivePendingChoiceReturnStack;

    UPROPERTY(Transient)
    FString DebugName;

    // 注册的对话显示器
    UPROPERTY(Transient)
    TScriptInterface<INarrRailDialoguePresenterInterface> DialoguePresenter;

    // 全局活跃会话列表
    static TArray<UNarrRailStorySession*> ActiveSessions;
};
