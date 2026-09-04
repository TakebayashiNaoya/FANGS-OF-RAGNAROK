/**
 * @file ShaderHotReload.h
 * @brief .hlsl の保存を見て PSO を作り直す。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "Core/Platform/DirectoryWatcher.h"
#include "RHI/ShaderCompiler.h"
#include <cstdint>
#include <string>
#include <vector>


struct ID3D12Device;


namespace fang::rhi
{
	class PipelinePool;

	/**
	 * @brief 直近の作り直しの結果。
	 * @details 画を見ている人へ「効いたのか、どこで失敗したのか」を返すためだけの入れ物。
	 *          エディタが毎フレーム読むので、文字列は確保の起きない固定長で持つ。
	 */
	struct ShaderReloadStatus
	{
		/** @brief lastMessage に入る最大の長さ（終端を含む）。FXC のエラーは数行になるので余裕を取る。 */
		static constexpr uint32_t MAX_MESSAGE_LENGTH = 1024;

		bool isWatching = false; /**< .hlsl の置き場を 1 か所でも見張れているか。 */

		uint32_t successCount = 0; /**< 起動してからの成功回数。 */
		uint32_t failureCount = 0; /**< 起動してからの失敗回数。 */

		bool wasLastAttemptSuccessful = true; /**< 直近の 1 回が通ったか。1 度も試していなければ true。 */

		/** @brief 成功なら作り直した本数、失敗なら FXC の出力（どのファイルの何行目かを含む）。 */
		char lastMessage[MAX_MESSAGE_LENGTH]{};
	};

#if FANG_ENABLE_HOT_RELOAD

	/**
	 * @brief .hlsl の置き場を見張り、変化があったらそこから来た PSO を作り直す。
	 * @details 変化したファイルの名前は見ず、そのディレクトリを出どころとするパイプラインをまとめて
	 *          作り直す ➡ .hlsli の取り込み関係を追わずに「Lighting.hlsli を直したら両方効く」が成り立つ。
	 *          作り直しは全部コンパイルが通ったときだけ行う。1 本でも通らなければ 1 個も差し替えない。
	 * @threading メインスレッドのみ。ReloadPipelines は、パスの記録が 1 本も走っていない地点でだけ呼べる。
	 */
	class ShaderHotReload
	{
	public:
		FANG_NON_COPYABLE(ShaderHotReload);

		/** @brief 同時に見張れるディレクトリの数。今は Renderer と Editor の 2 か所。 */
		static constexpr uint32_t MAX_WATCH_COUNT = 4;

		/** @brief 変化を拾ってから作り直すまでの待ち（秒）。保存中の連続通知をまとめ、書き込みの完了を待つ。 */
		static constexpr float DEBOUNCE_SECONDS = 0.15f;

		ShaderHotReload()  = default;
		~ShaderHotReload() = default;

		/** @brief 直近の作り直しの結果。 */
		[[nodiscard]] const ShaderReloadStatus& GetStatus() const { return m_status; }


	public:
		/**
		 * @brief .hlsl の置き場を見張りに加える。
		 * @details パイプラインを作るたびに呼ぶ。同じディレクトリは 1 回しか登録しない。
		 *          ソースツリーが見つからない構成（UWP・exe を外へ写した場合）では黙って何もしない
		 *          ➡ 描画はビルド時のバイトコードのまま動く。
		 * @param sourceRelativePath ソースツリーの根からの .hlsl の相対パス。nullptr なら何もしない。
		 */
		void WatchShaderDirectory(const char* sourceRelativePath);

		/** @brief 見張りを全部畳む。二重に呼んでも安全。 */
		void Shutdown();

		/**
		 * @brief 変化を拾い、作り直すべき頃合いかを返す。
		 * @details 変化が無ければ待ち時間 0 の問い合わせを見張りの数だけ打つだけで、ファイルは 1 バイトも読まない。
		 * @param deltaTimeSeconds 前フレームからの経過時間（秒）。待ちの消化に使う。
		 * @return true を返したフレームで ReloadPipelines を呼ぶこと。
		 */
		[[nodiscard]] bool ConsumeDueChange(float deltaTimeSeconds);

		/**
		 * @brief 変化のあったディレクトリから来たパイプラインを作り直す。
		 * @details GPU が今のパイプラインを読み終わってから呼ぶこと（差し替えで古い PSO が解放されるため）。
		 * @param pipelines 作り直す先の台帳。nullptr は不可。
		 */
		void ReloadPipelines(ID3D12Device& device, PipelinePool* pipelines);


	private:
		/** @brief 1 回の作り直しの中だけ効くコンパイル結果。同じ .hlsl を 2 回コンパイルしないために持つ。 */
		struct CompiledShader
		{
			const char* sourceRelativePath = nullptr; /**< 引くときの鍵。文字列リテラルなので寿命は気にしない。 */
			const char* entryPointName     = nullptr; /**< 同じファイルでも入口が違えば別のバイトコードになる。 */

			// TODO: Core の Array<T> ができたら std::vector をやめる。
			std::vector<uint8_t> bytecode;
		};

		/** @brief 見張っているディレクトリのどれかから来たパイプラインで、かつそこが変化していたら true。 */
		[[nodiscard]] bool IsReloadTarget(const char* sourceRelativePath) const;

		/**
		 * @brief まだコンパイルしていなければコンパイルして控える。
		 * @return 控えられなければ、その理由。
		 */
		[[nodiscard]] EnShaderCompileResult EnsureCompiled(
			const char*   sourceRelativePath,
			const char*   entryPointName,
			EnShaderStage stage,
			std::string*  outErrorMessage
		);

		/** @brief 控えたコンパイル結果を引く。無ければ nullptr。 */
		[[nodiscard]] const CompiledShader* FindCompiled(
			const char* sourceRelativePath,
			const char* entryPointName
		) const;

		/** @brief この回で見た変化の印を消す。作り直しが終わった（あきらめた）ときに呼ぶ。 */
		void ClearChangedFlags();

		/** @brief 結果を控えてログに出す。 */
		void SetStatus(bool isSuccessful, const char* message);


	private:
		DirectoryWatcher m_watchers[MAX_WATCH_COUNT];         /**< 見張り本体。 */
		std::string      m_watchDirectories[MAX_WATCH_COUNT]; /**< 根からの相対パス。出どころの突き合わせに使う。 */
		bool             m_isWatchChanged[MAX_WATCH_COUNT]{}; /**< この回で変化があった見張り。 */

		uint32_t m_watchCount = 0; /**< 登録済みの見張りの数。 */

		/** @brief ソースツリーの根（UTF-8 の絶対パス）。空なら見張れる場所が無い。 */
		std::string m_sourceRootPath;

		/** @brief 根を引きに行ったか。見つからない環境で毎回探し直さないための覚え。 */
		bool m_isSourceRootResolved = false;

		float m_pendingSecondsRemaining = 0.0f; /**< 0 より大きい間は待機中。0 を跨いだフレームで作り直す。 */

		/** @brief この待ちの中で、開けないファイルのために待ちを張り直したか。張り直すのは 1 回だけ。 */
		bool m_hasExtendedForFileAccess = false;

		std::vector<CompiledShader> m_compileCache; /**< 1 回の作り直しの中だけ効く控え。 */

		ShaderReloadStatus m_status; /**< エディタが読む結果。 */
	};

#endif
} // namespace fang::rhi
