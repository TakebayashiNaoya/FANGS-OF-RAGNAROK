/**
 * @file Pack.h
 * @brief 頂点属性を小さい型へ詰めるスカラー変換。
 */
#pragma once

#include <bit>
#include <cstdint>


namespace fang
{
	/**
	 * @brief float を IEEE 754 half（binary16）のビット列へ最近接丸め（偶数タイ）で変換する。
	 * @details half の正規化数で表せない小ささは非正規化数へ落とす ➡ 0 へ潰さない。
	 *          範囲を超える大きさは無限大へ飽和する。UV の圧縮に使う。
	 * @return half のビット列。float として読める値ではない。戻すのは UnpackFloat16。
	 */
	[[nodiscard]] constexpr uint16_t PackFloat16(float value)
	{
		const uint32_t bits      = std::bit_cast<uint32_t>(value);
		const uint32_t sign      = (bits >> 16) & 0x8000u;
		const uint32_t magnitude = bits & 0x7FFFFFFFu;

		// NaN と無限大。NaN は仮数を 1 bit だけ残して quiet NaN として伝える。
		if (magnitude >= 0x7F800000u)
		{
			return static_cast<uint16_t>(sign | 0x7C00u | ((magnitude > 0x7F800000u) ? 0x0200u : 0u));
		}

		// half の最大値（65504）で表せない大きさ ➡ 無限大へ飽和。
		if (magnitude >= 0x47800000u)
		{
			return static_cast<uint16_t>(sign | 0x7C00u);
		}

		// half の正規化数になる範囲。指数の下駄を履き替え、落とす 13 bit を丸める。
		// 丸めの繰り上がりが指数へ波及しても、ビット列が連続なのでそのまま正しい値になる。
		if (magnitude >= 0x38800000u)
		{
			const uint32_t rebased = magnitude - 0x38000000u;
			return static_cast<uint16_t>(sign | ((rebased + 0x0FFFu + ((rebased >> 13) & 1u)) >> 13));
		}

		// 非正規化数の最小刻み（2^-24）の半分に届かない ➡ 0。符号だけ残る。
		if (magnitude < 0x33000000u)
		{
			return static_cast<uint16_t>(sign);
		}

		// half の非正規化数になる範囲。暗黙の 1 を立ててから指数ぶんだけ右へずらす。
		const uint32_t mantissa   = (magnitude & 0x007FFFFFu) | 0x00800000u;
		const uint32_t shiftCount = 126u - (magnitude >> 23);

		uint32_t shifted = mantissa >> shiftCount;

		const uint32_t remainder = mantissa & ((1u << shiftCount) - 1u);
		const uint32_t halfStep  = 1u << (shiftCount - 1u);
		if (remainder > halfStep || (remainder == halfStep && (shifted & 1u) != 0u))
		{
			++shifted;
		}

		return static_cast<uint16_t>(sign | shifted);
	}


	/**
	 * @brief half のビット列を float へ戻す。
	 * @details 変換誤差の検証用。描画では使わない ➡ 展開は入力アセンブラの仕事。
	 */
	[[nodiscard]] constexpr float UnpackFloat16(uint16_t bits)
	{
		const uint32_t sign     = static_cast<uint32_t>(bits & 0x8000u) << 16;
		int32_t        exponent = (bits >> 10) & 0x1F;
		uint32_t       mantissa = bits & 0x03FFu;

		// NaN と無限大は float の同じ意味のビット列へ。
		if (exponent == 0x1F)
		{
			return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
		}

		// 非正規化数は暗黙の 1 が立つまで左へずらして正規化する。float なら正規化数で表せる。
		if (exponent == 0)
		{
			if (mantissa == 0u)
			{
				return std::bit_cast<float>(sign);
			}

			exponent = 1;
			while ((mantissa & 0x0400u) == 0u)
			{
				mantissa <<= 1;
				--exponent;
			}
			mantissa &= 0x03FFu;
		}

		// 指数の下駄を half の 15 から float の 127 へ履き替える。
		return std::bit_cast<float>(sign | (static_cast<uint32_t>(exponent + 112) << 23) | (mantissa << 13));
	}


	/**
	 * @brief [-1, 1] へクランプした値を 8 bit SNORM（-127〜127）へ最近接丸めで変換する。
	 * @details -128 は出さない ➡ GPU は -128 も -1.0 に飽和して読むが、-1.0 の表現を 1 つに保つ。
	 *          法線の圧縮に使う。
	 */
	[[nodiscard]] constexpr int8_t PackSignedNormalized8(float value)
	{
		const float clamped = (value < -1.0f) ? -1.0f : ((value > 1.0f) ? 1.0f : value);
		const float scaled  = clamped * 127.0f;

		// 0 から遠い側へ 0.5 を足してから整数へ切り捨てる ➡ 最近接丸め（タイは 0 から遠い側）。
		return static_cast<int8_t>((scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f));
	}
} // namespace fang
