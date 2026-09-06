/**
 * @file LevelGrowth.cpp
 * @brief 経験値からレベル・ステータス倍率を出す、状態を持たない計算。
 */
#include "Pch.h"
#include "Scene/LevelGrowth.h"
#include <algorithm>
#include <limits>


namespace fang
{
	int32_t ComputeExperienceToNextLevel(const LevelGrowthParameter& parameter, int32_t level)
	{
		return parameter.experienceToNextLevelPerLevel * level;
	}


	float ComputeStatusMultiplier(const LevelGrowthParameter& parameter, int32_t level)
	{
		if (level <= 1)
		{
			return 1.0f;
		}

		return 1.0f + parameter.statusRatePerLevel * static_cast<float>(level - 1);
	}


	void ClampLevelProgress(const LevelGrowthParameter& parameter, LevelProgress* progress)
	{
		const int32_t maximumLevel = std::max(parameter.maximumLevel, 1);

		progress->level            = std::clamp(progress->level, 1, maximumLevel);
		progress->experiencePoints = std::max(progress->experiencePoints, 0);

		if (progress->level >= maximumLevel)
		{
			const int32_t cap          = ComputeExperienceToNextLevel(parameter, progress->level);
			progress->experiencePoints = std::min(progress->experiencePoints, cap);
		}
	}


	LevelGrowthResult AddExperience(
		const LevelGrowthParameter& parameter,
		int32_t                     experiencePoints,
		LevelProgress*              progress
	)
	{
		ClampLevelProgress(parameter, progress);

		LevelGrowthResult result;
		if (experiencePoints <= 0)
		{
			return result;
		}

		const int32_t maximumAddable = std::numeric_limits<int32_t>::max() - progress->experiencePoints;
		progress->experiencePoints += std::min(experiencePoints, maximumAddable);

		const int32_t maximumLevel = std::max(parameter.maximumLevel, 1);
		while (progress->level < maximumLevel)
		{
			const int32_t requiredExperience = ComputeExperienceToNextLevel(parameter, progress->level);
			if (progress->experiencePoints < requiredExperience)
			{
				break;
			}

			progress->experiencePoints -= requiredExperience;
			++progress->level;
			++result.gainedLevelCount;
		}

		if (progress->level >= maximumLevel)
		{
			const int32_t cap          = ComputeExperienceToNextLevel(parameter, progress->level);
			progress->experiencePoints = std::min(progress->experiencePoints, cap);
		}

		return result;
	}
} // namespace fang
