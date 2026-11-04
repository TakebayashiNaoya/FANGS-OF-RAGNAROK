/**
 * @file Noncopyable.h
 * @brief コピー禁止クラス
 */
#pragma once


namespace fangEngine::core
{
	/**
	 * @brief 継承したクラスのコピーを禁止する
	 */
	class Noncopyable
	{
	public:
		Noncopyable(const Noncopyable&) = delete;
		Noncopyable& operator=(const Noncopyable&) = delete;


	protected:
		Noncopyable() = default;
		~Noncopyable() = default;
	};
} // namespace fangEngine::core
