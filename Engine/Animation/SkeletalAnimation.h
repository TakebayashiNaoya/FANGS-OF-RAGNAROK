/**
 * @file SkeletalAnimation.h
 * @brief スケルトンとクリップを読み、スキニング行列を作る。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Math/Matrix4x4.h"
#include "Core/Memory/UniquePtr.h"
#include <cstdint>
#include <span>


namespace fang
{
	/**
	 * @brief `.ozz` のスケルトンとクリップを持ち、指定の再生位置のスキニング行列を作る。
	 * @details 骨の姿勢を作る部分は ozz-animation に任せる。ozz の型はこのクラスの中に閉じ込めてあり、
	 *          公開ヘッダには出ていない ➡ 使う側は ozz のインクルードパスを持たなくてよい。
	 *          読み込みに失敗しても落ちない。GetJointCount() が 0 のままになるので、
	 *          呼び出し側は単位行列を渡してバインドポーズを描けばよい。
	 * @threading メインスレッドのみ。
	 */
	class SkeletalAnimation
	{
	public:
		FANG_NON_COPYABLE(SkeletalAnimation);
		FANG_NON_MOVABLE(SkeletalAnimation);

		SkeletalAnimation();
		~SkeletalAnimation();

		/**
		 * @brief スケルトンを読む。
		 * @param filePath `gltf2ozz` が出した `.ozz` の絶対パス。
		 * @return 開けない / 中身がスケルトンでない / 関節が多すぎる場合は false。理由はログに出す。
		 */
		[[nodiscard]] bool LoadSkeleton(const char* filePath);

		/**
		 * @brief クリップを読む。先にスケルトンを読んでおくこと。
		 * @param filePath `gltf2ozz` が出した `.ozz` の絶対パス。
		 * @return 開けない / 中身がクリップでない / 関節の数がスケルトンと合わない場合は false。
		 */
		[[nodiscard]] bool LoadClip(const char* filePath);

		/**
		 * @brief glTF の関節の並びから ozz の並びへの対応表を作る。
		 * @param gltfJointNames glTF の skin.joints の並びに並んだ関節名。
		 * @return 数が合わない / 名前がスケルトンに無い場合は false。
		 * @details `gltf2ozz` は関節を並べ替えるので、メッシュの JOINTS_0 が指す番号は ozz の番号と一致しない。
		 *          黙って番号をそのまま使うと、骨の対応がずれた姿勢が「それらしく」描かれてしまう。
		 */
		[[nodiscard]] bool BuildJointRemap(std::span<const char* const> gltfJointNames);

		/** @brief クリップの長さ（秒）。読めていなければ 0。 */
		[[nodiscard]] float GetClipDurationSeconds() const;

		/** @brief スケルトンの関節数。読めていなければ 0。 */
		[[nodiscard]] uint32_t GetJointCount() const;

		/** @brief スケルトンとクリップと対応表がそろっていれば true。 */
		[[nodiscard]] bool IsReady() const;

		/**
		 * @brief 再生位置の姿勢を作り、左手系のスキニング行列を書き出す。
		 * @param timeRatio           0〜1 の再生位置。AnimationPlayback::GetTimeRatio() の戻り値。
		 * @param inverseBindMatrices バインドポーズを打ち消す行列。glTF の関節の並び。
		 * @param outMatrices         書き出し先。inverseBindMatrices と同じ数だけ要る。
		 * @return そろっていない / 要素数が合わない / ozz の実行に失敗した場合は false。中身は書き換えない。
		 * @details const にしていないのは、サンプリングの作業領域を内部に持ち、毎回書き換えるため。
		 */
		[[nodiscard]] bool ComputeSkinningMatrices(
			float                      timeRatio,
			std::span<const Matrix4x4> inverseBindMatrices,
			std::span<Matrix4x4>       outMatrices
		);


	private:
		/**
		 * @brief ozz の型を持つ入れ物。定義は .cpp にある。
		 * @details ヘッダに ozz を出さないための壁。[ADR-021] で Pimpl はやめたが、ThirdParty のインクルード
		 *          パスを Animation の外へ広げないため、ここだけ間接を残す。毎フレーム 1 回しか通らない。
		 */
		struct OzzState;

		UniquePtr<OzzState> m_state;
	};
} // namespace fang
