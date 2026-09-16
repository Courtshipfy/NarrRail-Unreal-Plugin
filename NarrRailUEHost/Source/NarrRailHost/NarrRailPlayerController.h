#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Runtime/NarrRailStorySession.h"
#include "NarrRailPlayerController.generated.h"

class UNarrRailTypewriterController;

UCLASS()
class NARRRAILHOST_API ANarrRailPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    ANarrRailPlayerController();

    virtual void SetupInputComponent() override;

    UFUNCTION(BlueprintCallable, Category="NarrRail|Runtime")
    void BindNarrRailSession(UNarrRailStorySession* InSession);

    UFUNCTION(BlueprintCallable, Category="NarrRail|UI")
    void BindTypewriterController(UNarrRailTypewriterController* InTypewriter);

    UFUNCTION(BlueprintPure, Category="NarrRail|Runtime")
    UNarrRailStorySession* GetNarrRailSession() const { return Session; }

    UFUNCTION(BlueprintPure, Category="NarrRail|UI")
    UNarrRailTypewriterController* GetTypewriterController() const { return Typewriter; }

    /**
     * 把当前会话与全局状态存进一个槽。
     *
     * @param SlotName 槽名，默认演示槽。
     * @param UserIndex 平台用户索引，槽按用户隔离。
     * @return 是否写入成功。没有绑定会话时返回 false，不做任何写入。
     */
    UFUNCTION(BlueprintCallable, Category="NarrRail|Save")
    bool SaveNarrRailState(const FString& SlotName = TEXT("NarrRailDemoSave"), int32 UserIndex = 0);

    /**
     * 从槽里读回会话与全局状态。
     *
     * 槽不存在、槽内容不是 NarrRail 存档、故事资产加载失败、或任一道门拒绝，都会返回 false 且
     * 记录一条 Warning 日志。注意失败并非总是「什么都没变」：全局状态先于会话恢复，若全局恢复成功
     * 而会话随后被拒，全局状态会停在存档值上。这个残留是已知的，见
     * specs/0001-save-load-snapshots/data-model.md。
     *
     * @param SlotName 槽名，默认演示槽。
     * @param UserIndex 平台用户索引。
     * @param bRefreshPresenter 读档后是否重新驱动 UI 显示器。
     * @return 是否读取成功。
     */
    UFUNCTION(BlueprintCallable, Category="NarrRail|Save")
    bool LoadNarrRailState(const FString& SlotName = TEXT("NarrRailDemoSave"), int32 UserIndex = 0, bool bRefreshPresenter = true);

protected:
    UFUNCTION()
    void HandleAdvancePressed();

private:
    UPROPERTY(Transient, BlueprintReadOnly, Category="NarrRail|Runtime", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UNarrRailStorySession> Session = nullptr;

    UPROPERTY(Transient, BlueprintReadOnly, Category="NarrRail|UI", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UNarrRailTypewriterController> Typewriter = nullptr;
};
