#include "Runtime/NarrRailGlobalStateSubsystem.h"

namespace NarrRailGlobalState
{
// 当前受支持的全局状态快照版本。与会话快照的版本号各自独立：两者描述的是不同的东西，
// 一个变了不该逼另一个跟着变。
//
// 恢复路径同样写成按版本分派，今天只有一个分支。
constexpr int32 SupportedGlobalStateSnapshotVersion = 1;
}

void UNarrRailGlobalStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (GlobalVariables == nullptr)
	{
		GlobalVariables = NewObject<UNarrRailVariableContainer>(this);
	}
}

bool UNarrRailGlobalStateSubsystem::ApplyGlobalConfig(const UNarrRailGlobalConfigAsset* GlobalConfig, FString& OutErrorMessage)
{
	if (GlobalConfig == nullptr)
	{
		return true;
	}

	const FSoftObjectPath ConfigPath(GlobalConfig);
	if (AppliedConfigPaths.Contains(ConfigPath))
	{
		return true;
	}

	if (!ApplyGlobalConfigContent(GlobalConfig, OutErrorMessage))
	{
		return false;
	}

	AppliedConfigPaths.Add(ConfigPath);
	return true;
}

bool UNarrRailGlobalStateSubsystem::ApplyGlobalConfigContent(const UNarrRailGlobalConfigAsset* GlobalConfig, FString& OutErrorMessage)
{
	if (GlobalConfig == nullptr)
	{
		return true;
	}

	if (GlobalVariables == nullptr)
	{
		GlobalVariables = NewObject<UNarrRailVariableContainer>(this);
	}

	TArray<FNarrRailVariableDefinition> GlobalDefinitions;
	GlobalDefinitions.Reserve(GlobalConfig->Variables.Num());
	for (FNarrRailVariableDefinition Definition : GlobalConfig->Variables)
	{
		Definition.bGlobalScope = true;
		GlobalDefinitions.Add(Definition);
	}

	if (!GlobalVariables->AddDefinitions(GlobalDefinitions, true, OutErrorMessage))
	{
		return false;
	}

	for (const FNarrRailPresetSpeaker& Speaker : GlobalConfig->PresetSpeakers)
	{
		if (Speaker.SpeakerId == NAME_None)
		{
			continue;
		}

		if (const FNarrRailPresetSpeaker* Existing = PresetSpeakersById.Find(Speaker.SpeakerId))
		{
			if (Existing->DisplayName != Speaker.DisplayName || Existing->Color != Speaker.Color)
			{
				OutErrorMessage = FString::Printf(TEXT("Preset speaker '%s' is already defined with different data."), *Speaker.SpeakerId.ToString());
				return false;
			}
			continue;
		}

		PresetSpeakersById.Add(Speaker.SpeakerId, Speaker);
	}

	return true;
}

bool UNarrRailGlobalStateSubsystem::GetPresetSpeaker(const FName SpeakerId, FNarrRailPresetSpeaker& OutSpeaker) const
{
	if (const FNarrRailPresetSpeaker* Speaker = PresetSpeakersById.Find(SpeakerId))
	{
		OutSpeaker = *Speaker;
		return true;
	}

	return false;
}

FString UNarrRailGlobalStateSubsystem::ResolveSpeakerDisplayName(const FName SpeakerId) const
{
	FNarrRailPresetSpeaker Speaker;
	if (GetPresetSpeaker(SpeakerId, Speaker) && !Speaker.DisplayName.IsEmpty())
	{
		return Speaker.DisplayName;
	}

	return SpeakerId.ToString();
}

FNarrRailGlobalStateSnapshot UNarrRailGlobalStateSubsystem::GetGlobalStateSnapshot() const
{
	FNarrRailGlobalStateSnapshot Snapshot;

	// 显式盖章，理由同会话快照：字段默认值 0 是「缺失/畸形」的哨兵，采集不能产出它。
	Snapshot.SnapshotVersion = NarrRailGlobalState::SupportedGlobalStateSnapshotVersion;

	Snapshot.AppliedGlobalConfigPaths = AppliedConfigPaths;
	if (GlobalVariables != nullptr)
	{
		Snapshot.GlobalVariableSnapshot = GlobalVariables->GetSnapshot();
	}

	return Snapshot;
}

bool UNarrRailGlobalStateSubsystem::RestoreGlobalStateSnapshot(const FNarrRailGlobalStateSnapshot& Snapshot, FString& OutErrorMessage)
{
	// 版本门：按版本分派，只接受与受支持版本相等的值。更旧、更新、缺失（默认 0）、畸形（负数）
	// 一律拒绝——与会话快照的 FR-010 决定保持同一套语义，两个版本号虽然各自独立，但拒绝策略
	// 不该有两套。
	switch (Snapshot.SnapshotVersion)
	{
	case NarrRailGlobalState::SupportedGlobalStateSnapshotVersion:
		break;

	default:
		OutErrorMessage = FString::Printf(
			TEXT("Unsupported NarrRail global state snapshot version %d (supported: %d)."),
			Snapshot.SnapshotVersion,
			NarrRailGlobalState::SupportedGlobalStateSnapshotVersion);
		return false;
	}

	// 第一阶段：只解析，不改状态。
	//
	// 参考实现在这里先 PresetSpeakersById.Reset() 再逐个 TryLoad，于是「快照引用的配置已被删除」
	// 这条最可能发生的失败路径会把预设说话人清空之后才返回失败——一个半装好的全局状态。把解析
	// 全部提到改动之前，这条路径就变成零改动。
	TArray<const UNarrRailGlobalConfigAsset*> ResolvedConfigs;
	ResolvedConfigs.Reserve(Snapshot.AppliedGlobalConfigPaths.Num());
	for (const FSoftObjectPath& ConfigPath : Snapshot.AppliedGlobalConfigPaths)
	{
		if (ConfigPath.IsNull())
		{
			continue;
		}

		const UNarrRailGlobalConfigAsset* GlobalConfig = Cast<UNarrRailGlobalConfigAsset>(ConfigPath.TryLoad());
		if (GlobalConfig == nullptr)
		{
			OutErrorMessage = FString::Printf(TEXT("Failed to load NarrRail global config '%s'."), *ConfigPath.ToString());
			return false;
		}

		ResolvedConfigs.Add(GlobalConfig);
	}

	// 第二阶段：提交。到这里为止还没有改动过任何状态。
	if (GlobalVariables == nullptr)
	{
		GlobalVariables = NewObject<UNarrRailVariableContainer>(this);
	}

	PresetSpeakersById.Reset();
	for (const UNarrRailGlobalConfigAsset* GlobalConfig : ResolvedConfigs)
	{
		if (!ApplyGlobalConfigContent(GlobalConfig, OutErrorMessage))
		{
			// 已知残留：走到这里会使全局状态处于「部分应用」。第一阶段已经排除了配置缺失这类
			// 预期内失败，剩下能触发的只有内容冲突（同名变量定义不一致、同名预设说话人数据不同），
			// 那是作者侧的错误而不是读档时的运行时状况。彻底的事务化需要连变量定义一起回滚，
			// 而现在既没有真实存档、也无法在本机跑验证，加一层测不到的补偿逻辑得不偿失。
			// 触发条件：一旦出现需要从内容冲突中恢复的真实场景，就在此补上完整回滚。
			return false;
		}
	}

	GlobalVariables->RestoreFromSnapshot(Snapshot.GlobalVariableSnapshot);
	AppliedConfigPaths = Snapshot.AppliedGlobalConfigPaths;
	return true;
}
