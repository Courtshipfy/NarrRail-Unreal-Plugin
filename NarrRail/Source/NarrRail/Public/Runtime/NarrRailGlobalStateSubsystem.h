#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Runtime/NarrRailGlobalConfigAsset.h"
#include "Runtime/NarrRailVariableContainer.h"
#include "NarrRailGlobalStateSubsystem.generated.h"

// 全局状态的快照：跨会话存活的全局变量与已应用的全局配置。
//
// 它刻意与会话快照分开，两级各有各的版本号。全局变量的寿命长于任何一次剧情会话，把它们塞进
// 会话快照会让「会话 A 存档、会话 B 读档」这种正常用法丢掉全局进度。
//
// 暴露面与会话快照同理：字段不开放 Blueprint 读写，快照只能由采集产出、由恢复消费。
USTRUCT(BlueprintType)
struct NARRRAIL_API FNarrRailGlobalStateSnapshot
{
	GENERATED_BODY()

	// 布局版本，用于版本门。默认 0 是「缺失/畸形」的哨兵，详见会话快照里同样的说明。
	UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
	int32 SnapshotVersion = 0;

	// 已应用过哪些全局配置。必须随快照一起走：恢复时要先按这些路径把配置重新装上，才谈得上
	// 恢复变量值，否则当前配置声明、而快照值里没提到的变量就会不存在。
	UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
	TArray<FSoftObjectPath> AppliedGlobalConfigPaths;

	// 全局变量本身。
	UPROPERTY(VisibleAnywhere, SaveGame, Category = "NarrRail|Save")
	TMap<FName, FString> GlobalVariableSnapshot;
};

UCLASS(BlueprintType)
class NARRRAIL_API UNarrRailGlobalStateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "NarrRail|Global")
	bool ApplyGlobalConfig(const UNarrRailGlobalConfigAsset* GlobalConfig, FString& OutErrorMessage);

	UFUNCTION(BlueprintPure, Category = "NarrRail|Global")
	UNarrRailVariableContainer* GetGlobalVariableContainer() const
	{
		return GlobalVariables;
	}

	UFUNCTION(BlueprintPure, Category = "NarrRail|Global")
	bool GetPresetSpeaker(FName SpeakerId, FNarrRailPresetSpeaker& OutSpeaker) const;

	UFUNCTION(BlueprintPure, Category = "NarrRail|Global")
	FString ResolveSpeakerDisplayName(FName SpeakerId) const;

	// === 全局状态快照 ===

	/**
	 * 采集全局变量与已应用的全局配置。不改变任何状态。
	 */
	UFUNCTION(BlueprintPure, Category = "NarrRail|Save")
	FNarrRailGlobalStateSnapshot GetGlobalStateSnapshot() const;

	/**
	 * 用快照恢复全局状态。
	 *
	 * 先把快照记录的所有全局配置解析出来，全部解析成功之后才开始改动状态。因此「快照引用的配置
	 * 已经不在了」这条最可能发生的失败路径不会留下半装好的全局状态。
	 *
	 * @param Snapshot        要恢复的快照。
	 * @param OutErrorMessage 失败时的可读原因。
	 * @return 是否恢复成功。失败时已应用状态不会被部分覆盖（见 .cpp 的说明）。
	 */
	UFUNCTION(BlueprintCallable, Category = "NarrRail|Save")
	bool RestoreGlobalStateSnapshot(const FNarrRailGlobalStateSnapshot& Snapshot, FString& OutErrorMessage);

private:
	// 装载一个全局配置的内容（变量定义 + 预设说话人），不记录「已应用路径」。
	// 由 ApplyGlobalConfig 与 RestoreGlobalStateSnapshot 共用。
	bool ApplyGlobalConfigContent(const UNarrRailGlobalConfigAsset* GlobalConfig, FString& OutErrorMessage);

	UPROPERTY(Transient)
	TObjectPtr<UNarrRailVariableContainer> GlobalVariables;

	UPROPERTY(Transient)
	TArray<FSoftObjectPath> AppliedConfigPaths;

	UPROPERTY(Transient)
	TMap<FName, FNarrRailPresetSpeaker> PresetSpeakersById;
};
