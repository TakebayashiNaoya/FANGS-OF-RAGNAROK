/**
 * @file CharacterController.cpp
 * @brief 水平移動・向きの追従・置き物への応答。
 */
#include "Pch.h"
#include "Scene/CharacterController.h"
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

		/** @brief 水平成分が無い法線を倒す先。向きに意味は無く、決まっていることだけが要る。 */
		constexpr Vector3 HORIZONTAL_FALLBACK_NORMAL{ 1.0f, 0.0f, 0.0f };


		/**
		 * @brief 接触の法線を水平面へ落として長さ 1 にする。
		 * @details 押し戻しは水平面だけで解き、縦は接地が決める（ADR-061）。
		 *          真上・真下からの接触は水平成分が 0 に近く、正規化すると発散する ➡ 既定の水平方向を返す。
		 */
		Vector3 MakeHorizontalNormal(const Vector3& normal)
		{
			const Vector3 horizontal{ normal.x, 0.0f, normal.z };

			const float lengthSquared = LengthSquared(horizontal);
			if (lengthSquared <= DEGENERATE_LENGTH_SQUARED)
			{
				return HORIZONTAL_FALLBACK_NORMAL;
			}

			return horizontal * (1.0f / std::sqrt(lengthSquared));
		}


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
			// 縦成分は捨てる。縦は接地の持ち物で、押し戻しが書いてよいのは水平だけ（ADR-061）。
			const Vector3 outward = MakeHorizontalNormal(contact.normal);

			outSamples[writtenCount] = PenetrationSample{
				.normal = isFirst ? -outward : outward,
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


	ContactMoveResult MoveWithContacts(
		const Vector3&           position,
		const Vector3&           desiredDelta,
		std::span<const Contact> contacts,
		uint32_t                 userIndex
	)
	{
		PenetrationSample                        samples[MAX_PENETRATION_SAMPLE_COUNT]{};
		const uint32_t                           sampleCount = CollectPenetrations(contacts, userIndex, samples);
		const std::span<const PenetrationSample> touching(samples, sampleCount);

		ContactMoveResult result;
		result.position     = position + ResolvePenetration(touching);
		result.appliedDelta = SlideAlongNormals(desiredDelta, touching);
		result.position += result.appliedDelta;

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
