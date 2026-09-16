#include "NarrRailPlayerController.h"

#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "NarrRailHostSaveGame.h"
#include "Runtime/NarrRailGlobalConfigAsset.h"
#include "Runtime/NarrRailGlobalStateSubsystem.h"
#include "Runtime/NarrRailTypewriterController.h"

ANarrRailPlayerController::ANarrRailPlayerController()
{
    // 存档槽 UI 需要能点到按钮。
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
}

void ANarrRailPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (InputComponent)
    {
        InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ANarrRailPlayerController::HandleAdvancePressed);
    }
}

void ANarrRailPlayerController::BindNarrRailSession(UNarrRailStorySession* InSession)
{
    Session = InSession;
}

void ANarrRailPlayerController::BindTypewriterController(UNarrRailTypewriterController* InTypewriter)
{
    Typewriter = InTypewriter;
}

bool ANarrRailPlayerController::SaveNarrRailState(const FString& SlotName, const int32 UserIndex)
{
    if (Session == nullptr)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NarrRailHost] SaveNarrRailState failed: no bound session."));
        return false;
    }

    UNarrRailHostSaveGame* SaveGame = Cast<UNarrRailHostSaveGame>(
        UGameplayStatics::CreateSaveGameObject(UNarrRailHostSaveGame::StaticClass()));
    if (SaveGame == nullptr)
    {
        return false;
    }

    SaveGame->SessionSnapshot = Session->GetSessionSnapshot();
    SaveGame->SavedAtUtc = FDateTime::UtcNow().ToIso8601();

    // 全局状态不在会话快照里，需要单独采集（理由见 data-model.md：全局变量寿命长于单次会话）。
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        if (UNarrRailGlobalStateSubsystem* GlobalState = GameInstance->GetSubsystem<UNarrRailGlobalStateSubsystem>())
        {
            SaveGame->GlobalStateSnapshot = GlobalState->GetGlobalStateSnapshot();
        }
    }

    return UGameplayStatics::SaveGameToSlot(SaveGame, SlotName, UserIndex);
}

bool ANarrRailPlayerController::LoadNarrRailState(const FString& SlotName, const int32 UserIndex, const bool bRefreshPresenter)
{
    UNarrRailHostSaveGame* SaveGame = Cast<UNarrRailHostSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex));
    if (SaveGame == nullptr)
    {
        // 槽不存在，或者存在但不是 NarrRail 的存档（例如换过 USaveGame 子类）。
        // 这条路径不动任何状态，正是 quickstart 第 5 步要验证的行为。
        UE_LOG(LogTemp, Warning, TEXT("[NarrRailHost] LoadNarrRailState failed: slot '%s' has no NarrRail save."), *SlotName);
        return false;
    }

    // 存档指向的故事资产与当前会话不同时，就地按存档重建一个会话。
    // 这里为拿一个路径而采集了整份快照——运行时没有单独暴露 StoryAssetPath 的访问器，
    // 而采集是无副作用的（FR-006），所以这里选择不为此新增公开接口。
    const FSoftObjectPath& StoryAssetPath = SaveGame->SessionSnapshot.StoryAssetPath;
    const bool bNeedsSessionFromSave =
        Session == nullptr ||
        (!StoryAssetPath.IsNull() && Session->GetSessionSnapshot().StoryAssetPath != StoryAssetPath);

    if (bNeedsSessionFromSave)
    {
        UNarrRailStoryAsset* StoryAsset = Cast<UNarrRailStoryAsset>(StoryAssetPath.TryLoad());
        if (StoryAsset == nullptr)
        {
            UE_LOG(LogTemp, Warning, TEXT("[NarrRailHost] LoadNarrRailState failed: story asset '%s' could not be loaded."), *StoryAssetPath.ToString());
            return false;
        }

        const FSoftObjectPath& GlobalConfigPath = SaveGame->SessionSnapshot.GlobalConfigPath;
        UNarrRailGlobalConfigAsset* GlobalConfigAsset = nullptr;
        if (!GlobalConfigPath.IsNull())
        {
            GlobalConfigAsset = Cast<UNarrRailGlobalConfigAsset>(GlobalConfigPath.TryLoad());
            if (GlobalConfigAsset == nullptr)
            {
                UE_LOG(LogTemp, Warning, TEXT("[NarrRailHost] LoadNarrRailState failed: global config asset '%s' could not be loaded."), *GlobalConfigPath.ToString());
                return false;
            }
        }

        // 新建的会话会丢掉旧会话上的 UI 显示器注册，所以先取出来再装回去。
        TScriptInterface<INarrRailDialoguePresenterInterface> ExistingPresenter;
        if (Session != nullptr)
        {
            ExistingPresenter = Session->GetDialoguePresenter();
        }

        UNarrRailStorySession* NewSession = NewObject<UNarrRailStorySession>(this);
        if (NewSession == nullptr)
        {
            return false;
        }

        const FNarrRailRuntimeResult InitResult = GlobalConfigAsset != nullptr
            ? NewSession->InitializeWithGlobalConfig(StoryAsset, GlobalConfigAsset)
            : NewSession->Initialize(StoryAsset);
        if (InitResult.Code != ENarrRailRuntimeResultCode::Success)
        {
            UE_LOG(LogTemp, Warning, TEXT("[NarrRailHost] LoadNarrRailState failed: session init failed: %s"), *InitResult.Message);
            return false;
        }

        if (ExistingPresenter.GetObject() != nullptr)
        {
            NewSession->RegisterDialoguePresenter(ExistingPresenter);
        }

        Session = NewSession;
    }

    // 顺序：先全局、后会话（沿用参考实现）。
    //
    // 两条路径互有残留，这里如实记下来而不是假装是原子的：全局恢复成功而会话被门拒绝时，全局状态会
    // 停在存档值上；反过来若先恢复会话，则会出现「会话已换、全局没换」。真要两边都原子，需要运行时
    // 提供「只校验不提交」的入口，再让宿主两阶段提交——那是接口改动，在没有真实存档、也无法在本机
    // 验证的情况下不值得现在做。触发条件：一旦出现需要从这类残留中恢复的真实场景。
    if (UGameInstance* GameInstance = GetGameInstance())
    {
        if (UNarrRailGlobalStateSubsystem* GlobalState = GameInstance->GetSubsystem<UNarrRailGlobalStateSubsystem>())
        {
            FString ErrorMessage;
            if (!GlobalState->RestoreGlobalStateSnapshot(SaveGame->GlobalStateSnapshot, ErrorMessage))
            {
                UE_LOG(LogTemp, Warning, TEXT("[NarrRailHost] Restore global state failed: %s"), *ErrorMessage);
                return false;
            }
        }
    }

    const FNarrRailRuntimeResult Result = Session->RestoreSessionSnapshot(SaveGame->SessionSnapshot, bRefreshPresenter);
    if (Result.Code != ENarrRailRuntimeResultCode::Success)
    {
        UE_LOG(LogTemp, Warning, TEXT("[NarrRailHost] Restore session failed: %s"), *Result.Message);
        return false;
    }

    return true;
}

void ANarrRailPlayerController::HandleAdvancePressed()
{
    if (Typewriter && Typewriter->IsTyping())
    {
        Typewriter->CompleteImmediately();
        return;
    }

    if (Session == nullptr)
    {
        return;
    }

    const ENarrRailSessionState State = Session->GetSessionState();
    if (State == ENarrRailSessionState::WaitingForChoice)
    {
        return;
    }

    Session->Next();
}
