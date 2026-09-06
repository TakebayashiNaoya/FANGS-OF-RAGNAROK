/**
 * @file TuningRegistry.cpp
 * @brief 登録・行の組み立て・控えた書き戻しの実装。
 */
#include "Pch.h"
#include "Core/Reflection/TuningRegistry.h"
#include <algorithm>


namespace fang
{
	TuningRegistry& TuningRegistry::GetInstance()
	{
		static TuningRegistry s_instance;
		return s_instance;
	}


	bool TuningRegistry::Register(const char* displayName, void* object, const TypeInfo& typeInfo)
	{
		if (object == nullptr || m_entryCount >= MAX_ENTRY_COUNT)
		{
			return false;
		}

		m_entries[m_entryCount] = TuningEntry{ displayName, object, &typeInfo };
		++m_entryCount;
		return true;
	}


	void TuningRegistry::Clear()
	{
		m_entryCount        = 0;
		m_pendingWriteCount = 0;
		m_droppedWriteCount = 0;
	}


	std::span<const TuningEntry> TuningRegistry::GetEntries() const
	{
		return std::span<const TuningEntry>(m_entries.data(), m_entryCount);
	}


	TuningRowBuildResult TuningRegistry::BuildRows(std::span<TuningRow> outRows) const
	{
		TuningRowBuildResult total;

		for (uint32_t entryIndex = 0; entryIndex < m_entryCount; ++entryIndex)
		{
			const TuningEntry& entry = m_entries[entryIndex];

			const size_t               filledCount = std::min<size_t>(total.rowCount, outRows.size());
			const std::span<TuningRow> remaining   = outRows.subspan(filledCount);
			const TuningRowBuildResult entryResult =
				BuildTuningRows(*entry.typeInfo, entry.object, entryIndex, remaining);

			total.rowCount += entryResult.rowCount;
			total.depthLimitedFieldCount += entryResult.depthLimitedFieldCount;
			total.droppedRowCount += entryResult.droppedRowCount;
		}

		return total;
	}


	bool TuningRegistry::EnqueueWrite(const TuningRow& row, const FieldValue& value)
	{
		for (uint32_t index = 0; index < m_pendingWriteCount; ++index)
		{
			if (m_pendingWrites[index].address == row.address)
			{
				m_pendingWrites[index].value = value;
				return true;
			}
		}

		if (m_pendingWriteCount >= MAX_PENDING_WRITE_COUNT)
		{
			++m_droppedWriteCount;
			return false;
		}

		m_pendingWrites[m_pendingWriteCount] = PendingWrite{ row.address, row.type, row.range, value };
		++m_pendingWriteCount;
		return true;
	}


	uint32_t TuningRegistry::ApplyPendingWrites()
	{
		uint32_t appliedCount = 0;
		for (uint32_t index = 0; index < m_pendingWriteCount; ++index)
		{
			const PendingWrite& pendingWrite = m_pendingWrites[index];
			if (WriteFieldValue(pendingWrite.address, pendingWrite.type, pendingWrite.range, pendingWrite.value))
			{
				++appliedCount;
			}
		}

		m_pendingWriteCount = 0;
		return appliedCount;
	}


	uint32_t TuningRegistry::GetDroppedWriteCount() const
	{
		return m_droppedWriteCount;
	}
} // namespace fang
