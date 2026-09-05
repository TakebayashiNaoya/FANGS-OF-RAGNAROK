/**
 * @file CollisionDebugLines.cpp
 * @brief 形ごとのワイヤーフレームの組み立て。
 */
#include "Pch.h"
#include "Collision/CollisionDebugLines.h"
#include "Collision/CollisionMath.h"
#include "Core/Math/MathConstants.h"
#include <cmath>


namespace fang
{
	static_assert(
		MAX_SHAPE_LINE_COUNT >= SPHERE_LINE_COUNT && MAX_SHAPE_LINE_COUNT >= BOX_LINE_COUNT,
		"作業用の配列の大きさが、いちばん本数の多い形に足りていない"
	);


	namespace
	{
		/** @brief 書き込み先を使い切ったらそこで止める、線分の積み込み。 */
		class LineWriter
		{
		public:
			explicit LineWriter(std::span<DebugLineSegment> target)
				: m_target(target)
			{
			}

			/** @brief 積んだ本数。 */
			[[nodiscard]] uint32_t GetCount() const { return m_count; }


		public:
			/** @brief 1 本積む。使い切っていたら何もしない。 */
			void Add(const Vector3& from, const Vector3& to)
			{
				if (m_count >= m_target.size())
				{
					return;
				}

				m_target[m_count] = DebugLineSegment{ .from = from, .to = to };
				++m_count;
			}


		private:
			std::span<DebugLineSegment> m_target;

			uint32_t m_count = 0;
		};


		/**
		 * @brief 中心のまわりに円弧を積む。
		 * @param axisU        角度 0 の向き。長さ 1。
		 * @param axisV        角度 90 度の向き。長さ 1 で axisU と直交。
		 * @param sweepRadians 描く角度。1 周なら 2π、半円なら π。
		 * @param segmentCount 分割数。
		 */
		void AddArc(
			LineWriter*    writer,
			const Vector3& center,
			const Vector3& axisU,
			const Vector3& axisV,
			float          radius,
			float          sweepRadians,
			uint32_t       segmentCount
		)
		{
			Vector3 previousPoint = center + axisU * radius;
			for (uint32_t step = 1; step <= segmentCount; ++step)
			{
				const float   angle        = sweepRadians * static_cast<float>(step) / static_cast<float>(segmentCount);
				const Vector3 currentPoint = center + (axisU * std::cos(angle) + axisV * std::sin(angle)) * radius;

				writer->Add(previousPoint, currentPoint);
				previousPoint = currentPoint;
			}
		}


		/**
		 * @brief 向きに直交する 2 軸を作る。
		 * @param axis 長さ 1 の向き。潰れていたら既定の X / Z 軸を返す。
		 * @details 参照の軸は axis と平行にならないほうを選ぶ ➡ 外積が 0 にならず、正規化で NaN が出ない。
		 */
		void BuildPerpendicularAxes(const Vector3& axis, Vector3* outAxisU, Vector3* outAxisV)
		{
			if (LengthSquared(axis) <= DEGENERATE_LENGTH_SQUARED)
			{
				*outAxisU = Vector3{ 1.0f, 0.0f, 0.0f };
				*outAxisV = Vector3{ 0.0f, 0.0f, 1.0f };
				return;
			}

			const Vector3 reference =
				(std::abs(axis.y) < 0.9f) ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };

			*outAxisU = Normalize(Cross(reference, axis));
			*outAxisV = Cross(axis, *outAxisU);
		}


		/** @brief 球を直交する 3 つの円で描く。 */
		void AddSphereLines(LineWriter* writer, const Sphere& sphere)
		{
			const Vector3 axisX{ 1.0f, 0.0f, 0.0f };
			const Vector3 axisY{ 0.0f, 1.0f, 0.0f };
			const Vector3 axisZ{ 0.0f, 0.0f, 1.0f };

			AddArc(writer, sphere.center, axisX, axisY, sphere.radius, 2.0f * PI, CIRCLE_SEGMENT_COUNT);
			AddArc(writer, sphere.center, axisY, axisZ, sphere.radius, 2.0f * PI, CIRCLE_SEGMENT_COUNT);
			AddArc(writer, sphere.center, axisZ, axisX, sphere.radius, 2.0f * PI, CIRCLE_SEGMENT_COUNT);
		}


		/**
		 * @brief カプセルを両端の円・端の半円・側面の 4 本で描く。
		 * @details 潰れたカプセルでも本数を変えない（円が重なり、側面が長さ 0 になるだけ）
		 *          ➡ GetShapeLineCount が形の中身によらず一定になる。
		 */
		void AddCapsuleLines(LineWriter* writer, const Capsule& capsule)
		{
			const Vector3 axisOffset        = capsule.pointB - capsule.pointA;
			const float   axisLengthSquared = LengthSquared(axisOffset);
			const Vector3 axis =
				(axisLengthSquared > DEGENERATE_LENGTH_SQUARED) ? Normalize(axisOffset) : Vector3{ 0.0f, 1.0f, 0.0f };

			Vector3 axisU;
			Vector3 axisV;
			BuildPerpendicularAxes(axis, &axisU, &axisV);

			const float radius = capsule.radius;

			// 中心線に直交する円を両端に 1 つずつ。
			AddArc(writer, capsule.pointA, axisU, axisV, radius, 2.0f * PI, CIRCLE_SEGMENT_COUNT);
			AddArc(writer, capsule.pointB, axisU, axisV, radius, 2.0f * PI, CIRCLE_SEGMENT_COUNT);

			// 端の半球。中心線を含む 2 枚の面で半円を描く。pointA 側は外向きが -axis。
			AddArc(writer, capsule.pointA, axisU, -axis, radius, PI, HALF_CIRCLE_SEGMENT_COUNT);
			AddArc(writer, capsule.pointA, axisV, -axis, radius, PI, HALF_CIRCLE_SEGMENT_COUNT);
			AddArc(writer, capsule.pointB, axisU, axis, radius, PI, HALF_CIRCLE_SEGMENT_COUNT);
			AddArc(writer, capsule.pointB, axisV, axis, radius, PI, HALF_CIRCLE_SEGMENT_COUNT);

			// 側面。円の 4 か所を結ぶ。
			const Vector3 sideOffsets[4] = { axisU * radius, -axisU * radius, axisV * radius, -axisV * radius };
			for (const Vector3& sideOffset : sideOffsets)
			{
				writer->Add(capsule.pointA + sideOffset, capsule.pointB + sideOffset);
			}
		}


		/** @brief OBB を 12 本の辺で描く。頂点の並びは Aabb::GetCorners と同じ規則（x が最下位桁）。 */
		void AddBoxLines(LineWriter* writer, const OBB& box)
		{
			Vector3 corners[8];
			for (int cornerIndex = 0; cornerIndex < 8; ++cornerIndex)
			{
				Vector3 corner = box.center;
				for (int axisIndex = 0; axisIndex < 3; ++axisIndex)
				{
					const float sign       = ((cornerIndex >> axisIndex) & 1) != 0 ? 1.0f : -1.0f;
					const float halfExtent = GetComponent(box.halfExtents, axisIndex);

					corner += box.axes[axisIndex] * (halfExtent * sign);
				}

				corners[cornerIndex] = corner;
			}

			// 1 桁だけ違う頂点どうしが辺。x 方向 4 本、y 方向 4 本、z 方向 4 本。
			static constexpr int EDGES[BOX_LINE_COUNT][2] = {
				{ 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 },
				{ 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
			};

			for (const int (&edge)[2] : EDGES)
			{
				writer->Add(corners[edge[0]], corners[edge[1]]);
			}
		}
	} // namespace


	uint32_t GetShapeLineCount(const ColliderShape& shape)
	{
		switch (shape.type)
		{
			case EnShapeType::Sphere: return SPHERE_LINE_COUNT;
			case EnShapeType::Capsule: return CAPSULE_LINE_COUNT;
			case EnShapeType::OBB: return BOX_LINE_COUNT;
		}

		return 0;
	}


	uint32_t BuildShapeLines(const ColliderShape& shape, std::span<DebugLineSegment> outSegments)
	{
		LineWriter writer(outSegments);

		switch (shape.type)
		{
			case EnShapeType::Sphere: AddSphereLines(&writer, shape.sphere); break;
			case EnShapeType::Capsule: AddCapsuleLines(&writer, shape.capsule); break;
			case EnShapeType::OBB: AddBoxLines(&writer, shape.obb); break;
		}

		return writer.GetCount();
	}
} // namespace fang
