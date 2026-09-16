#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Runtime/NarrRailGlobalStateSubsystem.h"
#include "Runtime/NarrRailStorySession.h"
#include "NarrRailHostSaveGame.generated.h"

/**
 * 一个存档槽的内容：一份会话快照 + 一份全局状态快照。
 *
 * 运行时模块不知道槽的存在——它只产出和消费快照。槽名、平台用户索引、磁盘落盘这些都在宿主侧，
 * 由 UNarrRailHostSaveGame 与 ANarrRailPlayerController 的存取接口承担。
 *
 * 两份快照放在同一个槽里而不是分两个槽：它们必须一起读才自洽。分槽会让「会话读到了新档、全局还在
 * 旧档」变成一种可以发生、而且不容易被发现的状态。
 *
 * 暴露面（FR-007）：蓝图不需要读写快照内容，只需要显示时间戳。所以两个快照字段都是只读可见，
 * 只有 SavedAtUtc 对蓝图开放——而它也只开放读，因为它是 C++ 在存盘时写的。
 */
UCLASS()
class NARRRAILHOST_API UNarrRailHostSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    // 会话快照。蓝图不可写：它只应该由 SaveNarrRailState 产出。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    FNarrRailStorySessionSnapshot SessionSnapshot;

    // 全局状态快照，与会话快照同槽共存。
    UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
    FNarrRailGlobalStateSnapshot GlobalStateSnapshot;

    // 存盘时间，仅供存档界面显示。
    //
    // 刻意不参与任何恢复路径：如果哪天有人拿它来做版本判断或新旧比较，读档就会开始依赖一个纯展示
    // 用的字符串。它只被 SaveNarrRailState 写、被 UI 读。
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category = "NarrRail|Save")
    FString SavedAtUtc;
};
