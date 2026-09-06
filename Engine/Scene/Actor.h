/**
 * @file Actor.h
 * @brief Scene 上のオブジェクト 1 個への窓。
 */
#pragma once

#include "Core/Math/Matrix4x4.h"
#include "Core/Math/Vector3.h"
#include "Scene/ActorHandle.h"
#include <cstdint>
#include <span>


namespace fang
{
	class Scene;
	struct MeshRendererComponent;
	struct ColliderComponent;
	struct HealthComponent;

	/**
	 * @brief Scene 上のオブジェクト 1 個への窓。
	 * @details 中身は席を指すハンドルと Scene への参照だけで、座標もコンポーネントも 1 つも持たない
	 *          （真は Scene の配列に残る、ADR-041）。フレームをまたいで持ってよい。
	 *          値で写して構わない（16 バイト）。
	 * @threading 更新ジョブ 1 本から（Scene と同じ、ADR-030）。
	 */
	class Actor
	{
	public:
		Actor() = default;
		Actor(Scene& scene, ActorHandle handle);

		/** @brief 指している席が今も生きているか。既定構築なら false。 */
		[[nodiscard]] bool IsValid() const;

		[[nodiscard]] ActorHandle GetHandle() const { return m_handle; }

		/** @brief 席番号。掃引と接触が返す userIndex と同じ番号。 */
		[[nodiscard]] uint32_t GetIndex() const { return m_handle.index; }

		/** @brief 位置と Y 軸回りの向きを書く。 */
		[[nodiscard]] bool SetTransform(const Vector3& position, float rotationYRadians);

		[[nodiscard]] bool SetLocalMatrix(const Matrix4x4& localMatrix);

		/** @brief 直近の Update が作ったワールド行列。無効なら単位行列。 */
		[[nodiscard]] Matrix4x4 GetWorldMatrix() const;

		/** @brief ワールド行列の平行移動成分。無効なら原点。 */
		[[nodiscard]] Vector3 GetWorldPosition() const;

		/** @brief このフレームだけのスキニング行列を預ける。 */
		[[nodiscard]] bool SetSkinningMatrices(std::span<const Matrix4x4> matrices);

		[[nodiscard]] MeshRendererComponent* GetMeshRendererComponent() const;
		[[nodiscard]] ColliderComponent*     GetColliderComponent() const;
		[[nodiscard]] HealthComponent*       GetHealthComponent() const;

		/** @brief 破棄を予約する。席が空くのは次の破棄反映（Scene::DestroyObject と同じ）。 */
		void Destroy();


	private:
		Scene*      m_scene = nullptr;
		ActorHandle m_handle;
	};

	/**
	 * @brief 並びの中で最初に生きているものを返す。1 つも無ければ nullptr。
	 * @details 操作対象や標的のように「1 つを指し続ける」ものの付け替えに使う。破棄の通知を配らず、
	 *          毎フレーム選び直す（ADR-036）。並びの順がそのまま引き継ぎの順になる。
	 */
	[[nodiscard]] const Actor* FindFirstLiving(std::span<const Actor> actors);

	/** @brief 並びの中で生きているものの数。0 なら全滅。 */
	[[nodiscard]] uint32_t CountLiving(std::span<const Actor> actors);
} // namespace fang
