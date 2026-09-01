/**
 * @file UniquePtr.h
 * @brief アロケータを覚えている単独所有ポインタ。
 */
#pragma once

#include "Core/Memory/Allocator.h"


namespace fang
{
	/**
	 * @brief 所有権が 1 か所だけのポインタ。
	 * @details shared_ptr は使わない。生ポインタは借用を表す。
	 * @threading 所有スレッドのみ。
	 */
	template <typename T> class UniquePtr
	{
	public:
		FANG_NON_COPYABLE(UniquePtr);

		UniquePtr() = default;

		/** @brief New で allocator から作った object を引き取る。以後の解放はこちらがやる。 */
		UniquePtr(IAllocator& allocator, T* object)
			: m_allocator(&allocator)
			, m_object(object)
		{
		}

		UniquePtr(UniquePtr&& other) noexcept
			: m_allocator(other.m_allocator)
			, m_object(other.m_object)
		{
			other.m_allocator = nullptr;
			other.m_object    = nullptr;
		}

		UniquePtr& operator=(UniquePtr&& other) noexcept
		{
			if (this != &other)
			{
				Reset();
				m_allocator       = other.m_allocator;
				m_object          = other.m_object;
				other.m_allocator = nullptr;
				other.m_object    = nullptr;
			}

			return *this;
		}

		~UniquePtr() { Reset(); }

		/** @brief 中身を壊して空にする。 */
		void Reset()
		{
			if (m_object != nullptr)
			{
				Delete(*m_allocator, m_object);
				m_object = nullptr;
			}

			m_allocator = nullptr;
		}

		/** @brief 所有権を手放して生ポインタを返す。以後の解放は呼び出し側の責任になる。 */
		[[nodiscard]] T* Release()
		{
			T* released = m_object;
			m_object    = nullptr;
			m_allocator = nullptr;
			return released;
		}

		/** @brief 借用の生ポインタ。所有権は動かない。 */
		[[nodiscard]] FANG_FORCEINLINE T*   Get() const { return m_object; }
		[[nodiscard]] FANG_FORCEINLINE bool IsValid() const { return m_object != nullptr; }

		[[nodiscard]] FANG_FORCEINLINE T* operator->() const { return m_object; }
		[[nodiscard]] FANG_FORCEINLINE T& operator*() const { return *m_object; }


	private:
		IAllocator* m_allocator = nullptr; /**< m_object を Delete するときに使う。 */
		T*          m_object    = nullptr; /**< 所有しているオブジェクト。空なら nullptr。 */
	};

	/** @brief アロケータ上に作って UniquePtr で受け取る。 */
	template <typename T, typename... Args>
	[[nodiscard]] inline UniquePtr<T> MakeUnique(IAllocator& allocator, Args&&... args)
	{
		return UniquePtr<T>(allocator, New<T>(allocator, std::forward<Args>(args)...));
	}
} // namespace fang
