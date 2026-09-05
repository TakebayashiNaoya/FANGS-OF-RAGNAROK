/**
 * @file CharacterMovement.cpp
 * @brief 水平移動・向きの追従・置き物への応答。
 */
#include "Pch.h"
#include "Scene/CharacterMovement.h"
#include "Core/Math/MathConstants.h"
#include <cmath>


namespace fang
{
	namespace
	{
		/** @brief 長さの 2 乗をこれ以下とみなすと向きが決められない、という境目。 */
		constexpr float DEGENERATE_LENGTH_SQUARED = 1.0e-6f;

		/** @brief 押し出しを解き直す回数。角で 2 面に挟まれても 2 周目で収まる。 */
		constexpr int PENETRATION_RESOLVE_PASS_COUNT = 3;

		/** @brief 進む向きを削り直す回数。1 周目で削った先が別の壁へ入ることがある。 */
		constexpr int SLIDE_PASS_COUNT = 2;


		/** @brief 角度を -π 〜 π へ畳む。 */
		float WrapRadians(float radians)
		{
			constexpr float FULL_TURN = 2.0f * PI;

			float wrapped = std::fmod(radians + PI, FULL_TURN);
			if (wrapped < 0.0f)
			{
				wrapped += FULL_TURN;
			}

			return wrapped - PI;
		}
	} // namespace


	uint32_t CollectPenetrations(
		std::span<const Contact>     contacts,
		uint32_t                     userIndex,
		std::span<PenetrationSample> outSamples
	)
	{
		uint32_t writtenCount = 0;

		for (const Contact& contact : contacts)
		{
			const bool isFirst  = contact.userIndexA == userIndex;
			const bool isSecond = contact.userIndexB == userIndex;
			if (!isFirst && !isSecond)
			{
				continue;
			}

			if (writtenCount >= outSamples.size())
			{
				return writtenCount;
			}

			// 法線は 1 つ目から 2 つ目へ押す向き ➡ 自分が 1 つ目なら反転すると「自分を外へ出す向き」になる。
			outSamples[writtenCount] = PenetrationSample{
				.normal = isFirst ? -contact.normal : contact.normal,
				.depth  = contact.depth,
			};
			++writtenCount;
		}

		return writtenCount;
	}


	Vector3 ResolvePenetration(std::span<const PenetrationSample> samples)
	{
		Vector3 displacement;

		for (int pass = 0; pass < PENETRATION_RESOLVE_PASS_COUNT; ++pass)
		{
			bool hasMoved = false;

			for (const PenetrationSample& sample : samples)
			{
				// この向きについて、今の押し出し量で足りていない残り。足りていれば触らない
				// ➡ 同じ向きの接触が何件あっても二重に押さない。
				// 余白ぶんは押し切らずに残す ➡ 接触が消えず、次のフレームも進入方向を削り続けられる。
				const float remaining = sample.depth - PENETRATION_SKIN_CENTIMETERS - Dot(displacement, sample.normal);
				if (remaining <= 0.0f)
				{
					continue;
				}

				displacement += sample.normal * remaining;
				hasMoved = true;
			}

			if (!hasMoved)
			{
				break;
			}
		}

		return displacement;
	}


	Vector3 SlideAlongNormals(const Vector3& delta, std::span<const PenetrationSample> samples)
	{
		Vector3 result = delta;

		for (int pass = 0; pass < SLIDE_PASS_COUNT; ++pass)
		{
			bool hasChanged = false;

			for (const PenetrationSample& sample : samples)
			{
				// 外へ出る向きと逆を向いている成分が「壁へ食い込む量」。
				const float inwardAmount = Dot(result, sample.normal);
				if (inwardAmount >= 0.0f)
				{
					continue;
				}

				result -= sample.normal * inwardAmount;
				hasChanged = true;
			}

			if (!hasChanged)
			{
				break;
			}
		}

		return result;
	}


	Vector3 MakeMoveDelta(const Vector2& stick, float cameraYawRadians, float speed, float deltaTimeSeconds)
	{
		const float stickLengthSquared = stick.x * stick.x + stick.y * stick.y;
		if (stickLengthSquared <= DEGENERATE_LENGTH_SQUARED)
		{
			return Vector3{};
		}

		// カメラの方位が +X から測った角。画面の奥がカメラの正面、画面の右はそれを 90 度回した向き。
		const Vector3 forward{ std::cos(cameraYawRadians), 0.0f, std::sin(cameraYawRadians) };

		// 左手系 Y-up なので、画面の右は cross(上, 前) = (前.z, 0, -前.x)。
		const Vector3 right{ forward.z, 0.0f, -forward.x };

		const float scale = speed * deltaTimeSeconds;

		return (forward * stick.y + right * stick.x) * scale;
	}


	float TurnTowards(float currentRadians, float targetRadians, float maxStepRadians)
	{
		const float difference = WrapRadians(targetRadians - currentRadians);

		if (difference > maxStepRadians)
		{
			return WrapRadians(currentRadians + maxStepRadians);
		}

		if (difference < -maxStepRadians)
		{
			return WrapRadians(currentRadians - maxStepRadians);
		}

		return WrapRadians(targetRadians);
	}


	float GetYawFromDirection(const Vector3& direction)
	{
		if (direction.x * direction.x + direction.z * direction.z <= DEGENERATE_LENGTH_SQUARED)
		{
			return 0.0f;
		}

		return std::atan2(direction.z, direction.x);
	}
} // namespace fang
