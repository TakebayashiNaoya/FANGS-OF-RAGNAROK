/**
 * @file TangentGeneration.h
 * @brief 三角形の UV から頂点ごとの接線を作る。
 */
#pragma once

#include "Core/Math/Vector2.h"
#include "Core/Math/Vector3.h"
#include "Core/Math/Vector4.h"
#include <cstdint>
#include <span>


namespace fang
{
	/**
	 * @brief 三角形の UV から頂点ごとの接線を作る。
	 * @param positions   頂点の位置。
	 * @param normals     頂点の法線。positions と同じ数。数が違えば (0, 1, 0) として扱う。
	 * @param texCoords   頂点の UV。positions と同じ数。数が違えばどの三角形も退化するので、
	 *                    出力は法線と直交する既定の軸で埋まる。
	 * @param indices     三角形リスト。3 個そろわない末尾の端数は読まない。
	 * @param outTangents 書き込み先。positions と同じ数であること。違えば何も書かずに戻る。
	 *                    xyz = 正規化した接線、w = 従法線の符号（+1 か -1）。
	 * @details 三角形ごとに UV の微分から ∂P/∂u と ∂P/∂v を出して頂点へ足し込み、最後に法線に対して
	 *          グラム・シュミットで直交化する。従法線は glTF の規約に合わせて ∂P/∂v ➡ シェーダ側は
	 *          cross(normal, tangent.xyz) * tangent.w で復元する。
	 *          UV が退化している三角形（面積 0）は足し込まない。何も足されなかった頂点には法線と
	 *          直交する任意の軸を入れる ➡ 接線が 0 ベクトルになって面が真っ黒になるのを防ぐ。
	 * @threading 引数だけを見る純関数。呼び出し側のスレッドで完結する。
	 */
	void GenerateTangents(
		std::span<const Vector3>  positions,
		std::span<const Vector3>  normals,
		std::span<const Vector2>  texCoords,
		std::span<const uint16_t> indices,
		std::span<Vector4>        outTangents
	);
} // namespace fang
