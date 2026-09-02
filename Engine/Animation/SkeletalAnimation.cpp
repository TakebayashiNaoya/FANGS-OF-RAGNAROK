/**
 * @file SkeletalAnimation.cpp
 * @brief スケルトンとクリップの読み込み、姿勢のサンプリング、スキニング行列の生成。
 */
#include "Pch.h"
#include "Animation/SkeletalAnimation.h"
#include "Animation/AnimationLog.h"
#include "Core/Memory/Allocator.h"

// 上流のコードは /W4 /WX を想定していないので、この TU の中だけ警告を落とす。
// ThirdParty は改変しない方針なので、抑えるのは取り込む側の責任になる。
#pragma warning(push, 0)
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/base/containers/vector.h"
#include "ozz/base/io/archive.h"
#include "ozz/base/io/stream.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/span.h"
#pragma warning(pop)

#include <cstring>


FANG_DEFINE_LOG_CATEGORY(Animation);


namespace fang
{
	/**
	 * @brief ozz の持ち物をまとめた入れ物。
	 * @details ヘッダに ozz を出さないため、定義をここに置いている。作業領域は読み込みのときに
	 *          関節数ぶん確保して以後動かさない ➡ 毎フレームのヒープ確保が 0 になる。
	 */
	struct SkeletalAnimation::OzzState
	{
		ozz::animation::Skeleton             skeleton;
		ozz::animation::Animation            animation;
		ozz::animation::SamplingJob::Context samplingContext;

		/** @brief サンプリングの出力。ozz の並びで、4 関節を 1 要素にまとめた形（SoA）。 */
		ozz::vector<ozz::math::SoaTransform> localTransforms;

		/** @brief 親をたどって合成した姿勢。ozz の並び。 */
		ozz::vector<ozz::math::Float4x4> modelMatrices;

		/** @brief glTF の関節の番号 ➡ ozz の関節の番号。BuildJointRemap が作る。 */
		ozz::vector<uint16_t> jointRemap;

		bool hasSkeleton = false;
		bool hasClip     = false;
	};


	namespace
	{
		/**
		 * @brief ozz の行列を Matrix4x4 へ写す。
		 * @details ozz は列を 4 本並べて持つ（列ベクトル規約）ので、その 16 float は行優先ストレージ +
		 *          行ベクトル規約の Matrix4x4 としてそのまま正しい。➡ ここで転置を書き足すと逆に壊れる。
		 *          C++ から HLSL へ転置せずに渡すのと同じ理屈で、同じ勘違いを 2 回できる場所になっている。
		 */
		[[nodiscard]] Matrix4x4 ToMatrix4x4(const ozz::math::Float4x4& source)
		{
			Matrix4x4 result;
			for (int column = 0; column < 4; ++column)
			{
				ozz::math::StorePtrU(source.cols[column], &result.m[column][0]);
			}

			return result;
		}

		/**
		 * @brief `.ozz` を開いて中身を読む。
		 * @return 開けない / タグが違う場合は false。
		 */
		template <typename T> [[nodiscard]] bool LoadOzzArchive(const char* filePath, const char* what, T* outObject)
		{
			if (filePath == nullptr || filePath[0] == '\0')
			{
				FANG_LOG_ERROR(Animation, "{}のパスが空", what);
				return false;
			}

			ozz::io::File file(filePath, "rb");
			if (!file.opened())
			{
				FANG_LOG_ERROR(Animation, "{}を開けなかった: {}", what, filePath);
				return false;
			}

			ozz::io::IArchive archive(&file);
			if (!archive.TestTag<T>())
			{
				FANG_LOG_ERROR(Animation, "{}として読めない中身だった: {}", what, filePath);
				return false;
			}

			archive >> *outObject;

			return true;
		}
	} // namespace


	SkeletalAnimation::SkeletalAnimation()
		: m_state(HeapAllocator::GetInstance(), New<OzzState>(HeapAllocator::GetInstance()))
	{
	}


	SkeletalAnimation::~SkeletalAnimation() = default;


	bool SkeletalAnimation::LoadSkeleton(const char* filePath)
	{
		OzzState& state   = *m_state;
		state.hasSkeleton = false;
		state.hasClip     = false;
		state.jointRemap.clear();

		if (!LoadOzzArchive(filePath, "スケルトン", &state.skeleton))
		{
			return false;
		}

		if (state.skeleton.num_joints() <= 0)
		{
			FANG_LOG_ERROR(Animation, "スケルトンに関節が無い: {}", filePath);
			return false;
		}

		// 姿勢の置き場は関節数で決まる。ここで取り切って、以後は毎フレーム使い回す。
		state.localTransforms.resize(static_cast<size_t>(state.skeleton.num_soa_joints()));
		state.modelMatrices.resize(static_cast<size_t>(state.skeleton.num_joints()));
		state.samplingContext.Resize(state.skeleton.num_joints());

		state.hasSkeleton = true;

		FANG_LOG_INFO(Animation, "スケルトンを読んだ: 関節 {}: {}", state.skeleton.num_joints(), filePath);

		return true;
	}


	bool SkeletalAnimation::LoadClip(const char* filePath)
	{
		OzzState& state = *m_state;
		state.hasClip   = false;

		if (!state.hasSkeleton)
		{
			FANG_LOG_ERROR(Animation, "スケルトンより先にクリップを読もうとした: {}", filePath);
			return false;
		}

		if (!LoadOzzArchive(filePath, "クリップ", &state.animation))
		{
			return false;
		}

		// トラック数が関節数と違うクリップは別のスケルトン用。混ぜると姿勢が黙って崩れる。
		if (state.animation.num_tracks() != state.skeleton.num_joints())
		{
			FANG_LOG_ERROR(
				Animation,
				"クリップのトラック数がスケルトンの関節数と違う: {} と {}: {}",
				state.animation.num_tracks(),
				state.skeleton.num_joints(),
				filePath
			);
			return false;
		}

		state.hasClip = true;

		FANG_LOG_INFO(
			Animation,
			"クリップを読んだ: {:.3f} 秒 / トラック {}: {}",
			state.animation.duration(),
			state.animation.num_tracks(),
			filePath
		);

		return true;
	}


	bool SkeletalAnimation::BuildJointRemap(std::span<const char* const> gltfJointNames)
	{
		OzzState& state = *m_state;
		state.jointRemap.clear();

		if (!state.hasSkeleton)
		{
			FANG_LOG_ERROR(Animation, "スケルトンを読む前に対応表を作ろうとした");
			return false;
		}

		const ozz::span<const char* const> ozzJointNames = state.skeleton.joint_names();
		if (gltfJointNames.size() != ozzJointNames.size())
		{
			FANG_LOG_ERROR(
				Animation,
				"glTF とスケルトンで関節の数が違う: {} と {}",
				gltfJointNames.size(),
				ozzJointNames.size()
			);
			return false;
		}

		state.jointRemap.resize(gltfJointNames.size());
		for (size_t gltfIndex = 0; gltfIndex < gltfJointNames.size(); ++gltfIndex)
		{
			bool isFound = false;
			for (size_t ozzIndex = 0; ozzIndex < ozzJointNames.size(); ++ozzIndex)
			{
				if (std::strcmp(gltfJointNames[gltfIndex], ozzJointNames[ozzIndex]) == 0)
				{
					state.jointRemap[gltfIndex] = static_cast<uint16_t>(ozzIndex);
					isFound                     = true;
					break;
				}
			}

			if (!isFound)
			{
				FANG_LOG_ERROR(Animation, "スケルトンに無い関節がメッシュ側にある: {}", gltfJointNames[gltfIndex]);
				state.jointRemap.clear();
				return false;
			}
		}

		return true;
	}


	float SkeletalAnimation::GetClipDurationSeconds() const
	{
		return m_state->hasClip ? m_state->animation.duration() : 0.0f;
	}


	uint32_t SkeletalAnimation::GetJointCount() const
	{
		return m_state->hasSkeleton ? static_cast<uint32_t>(m_state->skeleton.num_joints()) : 0;
	}


	bool SkeletalAnimation::IsReady() const
	{
		return m_state->hasSkeleton && m_state->hasClip && !m_state->jointRemap.empty();
	}


	bool SkeletalAnimation::ComputeSkinningMatrices(
		float                      timeRatio,
		std::span<const Matrix4x4> inverseBindMatrices,
		std::span<Matrix4x4>       outMatrices
	)
	{
		if (!IsReady())
		{
			return false;
		}

		OzzState& state = *m_state;
		if (inverseBindMatrices.size() != outMatrices.size() || inverseBindMatrices.size() != state.jointRemap.size())
		{
			FANG_LOG_ERROR(
				Animation,
				"スキニング行列の要素数が合っていない: 逆バインド {} / 出力 {} / 対応表 {}",
				inverseBindMatrices.size(),
				outMatrices.size(),
				state.jointRemap.size()
			);
			return false;
		}

		ozz::animation::SamplingJob samplingJob;
		samplingJob.animation = &state.animation;
		samplingJob.context   = &state.samplingContext;
		samplingJob.ratio     = timeRatio;
		samplingJob.output    = ozz::make_span(state.localTransforms);
		if (!samplingJob.Run())
		{
			FANG_LOG_ERROR(Animation, "姿勢のサンプリングに失敗した");
			return false;
		}

		ozz::animation::LocalToModelJob localToModelJob;
		localToModelJob.skeleton = &state.skeleton;
		localToModelJob.input    = ozz::make_span(state.localTransforms);
		localToModelJob.output   = ozz::make_span(state.modelMatrices);
		if (!localToModelJob.Run())
		{
			FANG_LOG_ERROR(Animation, "親をたどった姿勢の合成に失敗した");
			return false;
		}

		for (size_t gltfIndex = 0; gltfIndex < outMatrices.size(); ++gltfIndex)
		{
			const ozz::math::Float4x4& jointMatrix = state.modelMatrices[state.jointRemap[gltfIndex]];

			// 行ベクトル規約なので、先に効かせる逆バインドが左に来る。
			outMatrices[gltfIndex] =
				Multiply(inverseBindMatrices[gltfIndex], ConvertToLeftHanded(ToMatrix4x4(jointMatrix)));
		}

		return true;
	}
} // namespace fang
