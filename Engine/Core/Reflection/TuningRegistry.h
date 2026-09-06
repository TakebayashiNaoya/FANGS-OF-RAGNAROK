/**
 * @file TuningRegistry.h
 * @brief 画面に出す調整値の登録簿と、つまみが控えた書き戻し。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Reflection/TuningRow.h"
#include <array>
#include <span>


namespace fang
{
	/** @brief 画面に出す実体 1 件。表示名とアドレスと素性の 3 つだけ。 */
	struct TuningEntry
	{
		const char*     displayName = nullptr;
		void*           object      = nullptr;
		const TypeInfo* typeInfo    = nullptr;
	};

	/**
	 * @brief 画面に出す調整値の登録簿と、つまみが控えた書き戻し。
	 * @details 出すものを決めるのは実体の持ち主。ここは表示名 + アドレス + TypeInfo を並べるだけで、
	 *          Game の型も ImGui も 1 つも知らない ➡ Editor は void* と const TypeInfo& しか触らない。
	 *          Release では誰も登録しないので空のまま（固定長の配列 1 本ぶんの静的領域だけが残る）。
	 * @threading メインスレッドのみ。ApplyPendingWrites は「更新ジョブが 1 本も走っていない時点」で呼ぶこと
	 *            （フレームループが RunFrame の直後に呼ぶ）。登録した実体を書き換えるのはこの関数だけ。
	 */
	class TuningRegistry
	{
	public:
		FANG_NON_COPYABLE(TuningRegistry);
		FANG_NON_MOVABLE(TuningRegistry);

		/** @brief 登録できる件数。今は 5 件。 */
		static constexpr uint32_t MAX_ENTRY_COUNT = 16;

		/** @brief 1 フレームに控えられる書き戻しの件数。同じアドレスへの 2 回目は上書きなので、実質つまみの数。 */
		static constexpr uint32_t MAX_PENDING_WRITE_COUNT = 32;

		TuningRegistry()  = default;
		~TuningRegistry() = default;

		/** @brief プロセスで 1 つの登録簿。Game が載せ、Editor が舐める。 */
		[[nodiscard]] static TuningRegistry& GetInstance();

		/**
		 * @brief 実体を 1 件載せる。
		 * @param object 呼び出し側が Clear まで生かし続けること。
		 * @return null を渡した、または上限を超えたら false（黙って捨てない）。
		 */
		[[nodiscard]] bool Register(const char* displayName, void* object, const TypeInfo& typeInfo);

		/** @brief FANG_REFLECT 付きの型を載せる糖衣。Game 側はこちらだけを使う。 */
		template <typename T> [[nodiscard]] bool Register(const char* displayName, T* object)
		{
			return object != nullptr && Register(displayName, object, T::GetTypeInfo());
		}

		/** @brief 全部下ろす。控えている書き戻しも捨てる。二重に呼んでも安全。 */
		void Clear();

		[[nodiscard]] std::span<const TuningEntry> GetEntries() const;

		/** @brief 登録の順に行を組み立てる。件ごとの行は連続して並ぶ。 */
		[[nodiscard]] TuningRowBuildResult BuildRows(std::span<TuningRow> outRows) const;

		/**
		 * @brief つまみが動いたことを控える。実体はまだ書き換えない。
		 * @return 控え帳が満杯なら false（GetDroppedWriteCount が増える）。
		 * @details 同じアドレスへの 2 回目は上書きする ➡ スライダーを 1 秒つかんでも 1 件しか積まれない。
		 */
		[[nodiscard]] bool EnqueueWrite(const TuningRow& row, const FieldValue& value);

		/**
		 * @brief 控えた書き戻しを実体へ入れ、控え帳を空にする。
		 * @return 入れた件数。
		 * @details 呼ぶのは更新ジョブが走っていない時点だけ。範囲の丸めは WriteFieldValue が行う。
		 */
		uint32_t ApplyPendingWrites();

		/** @brief 控え帳が満杯で捨てた件数。パネルが出す。 */
		[[nodiscard]] uint32_t GetDroppedWriteCount() const;


	private:
		/** @brief 控えた書き戻し 1 件。行を持ち回らず、書くのに要る 4 つだけを写す。 */
		struct PendingWrite
		{
			void*       address = nullptr;
			EnFieldType type    = EnFieldType::Float;
			Range       range;
			FieldValue  value;
		};

		std::array<TuningEntry, MAX_ENTRY_COUNT>          m_entries;
		std::array<PendingWrite, MAX_PENDING_WRITE_COUNT> m_pendingWrites;

		uint32_t m_entryCount        = 0;
		uint32_t m_pendingWriteCount = 0;
		uint32_t m_droppedWriteCount = 0;
	};
} // namespace fang
