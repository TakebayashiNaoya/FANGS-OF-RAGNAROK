/**
 * @file ShaderHotReload.cpp
 * @brief .hlsl の保存を見て PSO を作り直す処理の実装。
 */
#include "Pch.h"
#include "RHI/ShaderHotReload.h"
#include "Core/Platform/AssetPath.h"
#include "RHI/PipelinePool.h"
#include "RHI/RHILog.h"
#include <cstdio>
#include <span>
#include <string_view>


namespace fang::rhi
{
#if FANG_ENABLE_HOT_RELOAD

	namespace
	{
		/** @brief 相対パスからディレクトリの部分だけを取り出す。区切りが無ければ空。 */
		std::string_view GetDirectoryPart(const char* relativePath)
		{
			const std::string_view path(relativePath);

			const size_t separatorIndex = path.find_last_of("/\\");
			if (separatorIndex == std::string_view::npos)
			{
				return std::string_view();
			}

			return path.substr(0, separatorIndex);
		}


		/** @brief 根と相対パスを繋ぐ。区切りは Windows の \ に揃える。 */
		std::string MakeAbsolutePath(const std::string& rootPath, std::string_view relativePath)
		{
			std::string path = rootPath;
			path += '\\';
			path += relativePath;

			for (char& character : path)
			{
				if (character == '/')
				{
					character = '\\';
				}
			}

			return path;
		}
	} // namespace


	void ShaderHotReload::WatchShaderDirectory(const char* sourceRelativePath)
	{
		if (sourceRelativePath == nullptr || sourceRelativePath[0] == '\0')
		{
			return;
		}

		// 根は 1 回だけ引く。見つからない環境（UWP・exe を外へ写した場合）では以降ずっと空のまま。
		if (!m_isSourceRootResolved)
		{
			m_sourceRootPath       = GetSourceRootPath();
			m_isSourceRootResolved = true;
		}

		if (m_sourceRootPath.empty())
		{
			return;
		}

		const std::string_view directoryPart = GetDirectoryPart(sourceRelativePath);
		if (directoryPart.empty())
		{
			return;
		}

		for (uint32_t index = 0; index < m_watchCount; ++index)
		{
			if (m_watchDirectories[index] == directoryPart)
			{
				return;
			}
		}

		if (m_watchCount >= MAX_WATCH_COUNT)
		{
			FANG_LOG_ERROR(RHI, "シェーダーの見張りが上限に達した: {}", directoryPart);
			return;
		}

		const std::string absolutePath = MakeAbsolutePath(m_sourceRootPath, directoryPart);
		if (!m_watchers[m_watchCount].Initialize(absolutePath.c_str()))
		{
			FANG_LOG_ERROR(RHI, "シェーダーの見張りを立てられなかった: {}", absolutePath);
			return;
		}

		m_watchDirectories[m_watchCount] = directoryPart;
		m_isWatchChanged[m_watchCount]   = false;
		++m_watchCount;

		m_status.isWatching = true;

		FANG_LOG_INFO(RHI, "シェーダーの保存を見張る: {}", absolutePath);
	}


	void ShaderHotReload::Shutdown()
	{
		for (uint32_t index = 0; index < MAX_WATCH_COUNT; ++index)
		{
			m_watchers[index].Shutdown();
			m_watchDirectories[index].clear();
			m_isWatchChanged[index] = false;
		}

		m_watchCount              = 0;
		m_pendingSecondsRemaining = 0.0f;
		m_status.isWatching       = false;
		m_compileCache.clear();
	}


	bool ShaderHotReload::ConsumeDueChange(float deltaTimeSeconds)
	{
		bool hasNewChange = false;
		for (uint32_t index = 0; index < m_watchCount; ++index)
		{
			if (m_watchers[index].ConsumeChange())
			{
				m_isWatchChanged[index] = true;
				hasNewChange            = true;
			}
		}

		// 保存が続いている間は待ちを張り直す ➡ 書き込みの途中のファイルを掴まない。
		if (hasNewChange)
		{
			m_pendingSecondsRemaining  = DEBOUNCE_SECONDS;
			m_hasExtendedForFileAccess = false;
			return false;
		}

		if (m_pendingSecondsRemaining <= 0.0f)
		{
			return false;
		}

		m_pendingSecondsRemaining -= deltaTimeSeconds;
		return m_pendingSecondsRemaining <= 0.0f;
	}


	void ShaderHotReload::ReloadPipelines(ID3D12Device& device, PipelinePool* pipelines)
	{
		FANG_ASSERT(pipelines != nullptr, "台帳が nullptr");

		m_compileCache.clear();

		const uint32_t entryCount = pipelines->GetEntryCount();
		std::string    errorMessage;

		// ① 対象を全部コンパイルする。1 本でも通らなければ 1 個も差し替えない。
		for (uint32_t index = 0; index < entryCount; ++index)
		{
			const PipelinePool::Entry& entry = pipelines->GetByIndex(index);
			if (!entry.isAlive || !IsReloadTarget(entry.recipe.vertexShader.sourceRelativePath))
			{
				continue;
			}

			EnShaderCompileResult result = EnsureCompiled(
				entry.recipe.vertexShader.sourceRelativePath,
				entry.recipe.vertexShader.entryPointName,
				EnShaderStage::Vertex,
				&errorMessage
			);

			if (result == EnShaderCompileResult::Success)
			{
				result = EnsureCompiled(
					entry.recipe.pixelShader.sourceRelativePath,
					entry.recipe.pixelShader.entryPointName,
					EnShaderStage::Pixel,
					&errorMessage
				);
			}

			// 保存の途中を掴んだ見込み ➡ 失敗として数えず、1 回だけ待ち直す。
			if (result == EnShaderCompileResult::FileUnavailable)
			{
				if (!m_hasExtendedForFileAccess)
				{
					m_hasExtendedForFileAccess = true;
					m_pendingSecondsRemaining  = DEBOUNCE_SECONDS;
					return;
				}

				SetStatus(false, "シェーダーのファイルを開けなかった");
				ClearChangedFlags();
				return;
			}

			if (result == EnShaderCompileResult::CompileFailed)
			{
				SetStatus(false, errorMessage.c_str());
				ClearChangedFlags();
				return;
			}
		}

		// ② そろったバイトコードで枠の中身を差し替える。ハンドルは変わらない。
		uint32_t reloadedCount = 0;
		for (uint32_t index = 0; index < entryCount; ++index)
		{
			const PipelinePool::Entry& entry = pipelines->GetByIndex(index);
			if (!entry.isAlive || !IsReloadTarget(entry.recipe.vertexShader.sourceRelativePath))
			{
				continue;
			}

			const CompiledShader* vertexShader =
				FindCompiled(entry.recipe.vertexShader.sourceRelativePath, entry.recipe.vertexShader.entryPointName);
			if (vertexShader == nullptr)
			{
				continue;
			}

			// 深度専用のパイプラインはピクセルシェーダを持たない ➡ 空のまま渡す。
			const CompiledShader* pixelShader =
				FindCompiled(entry.recipe.pixelShader.sourceRelativePath, entry.recipe.pixelShader.entryPointName);

			std::span<const uint8_t> pixelBytecode;
			if (pixelShader != nullptr)
			{
				pixelBytecode = pixelShader->bytecode;
			}

			if (pipelines->Recreate(device, index, vertexShader->bytecode, pixelBytecode))
			{
				++reloadedCount;
			}
		}

		char message[ShaderReloadStatus::MAX_MESSAGE_LENGTH];
		std::snprintf(message, sizeof(message), "%u 本のパイプラインを作り直した", reloadedCount);

		SetStatus(true, message);
		ClearChangedFlags();
	}


	bool ShaderHotReload::IsReloadTarget(const char* sourceRelativePath) const
	{
		if (sourceRelativePath == nullptr)
		{
			return false;
		}

		const std::string_view directoryPart = GetDirectoryPart(sourceRelativePath);
		for (uint32_t index = 0; index < m_watchCount; ++index)
		{
			if (m_isWatchChanged[index] && m_watchDirectories[index] == directoryPart)
			{
				return true;
			}
		}

		return false;
	}


	EnShaderCompileResult ShaderHotReload::EnsureCompiled(
		const char*   sourceRelativePath,
		const char*   entryPointName,
		EnShaderStage stage,
		std::string*  outErrorMessage
	)
	{
		// 出どころを持たないシェーダは作り直しの対象外。深度専用パイプラインの PS がこれに当たる。
		if (sourceRelativePath == nullptr || entryPointName == nullptr)
		{
			return EnShaderCompileResult::Success;
		}

		if (FindCompiled(sourceRelativePath, entryPointName) != nullptr)
		{
			return EnShaderCompileResult::Success;
		}

		CompiledShader compiled;
		compiled.sourceRelativePath = sourceRelativePath;
		compiled.entryPointName     = entryPointName;

		const std::string absolutePath = MakeAbsolutePath(m_sourceRootPath, sourceRelativePath);

		const EnShaderCompileResult result =
			CompileShaderFromFile(absolutePath.c_str(), entryPointName, stage, &compiled.bytecode, outErrorMessage);
		if (result != EnShaderCompileResult::Success)
		{
			return result;
		}

		m_compileCache.push_back(std::move(compiled));
		return EnShaderCompileResult::Success;
	}


	const ShaderHotReload::CompiledShader* ShaderHotReload::FindCompiled(
		const char* sourceRelativePath,
		const char* entryPointName
	) const
	{
		if (sourceRelativePath == nullptr || entryPointName == nullptr)
		{
			return nullptr;
		}

		for (const CompiledShader& compiled : m_compileCache)
		{
			// 鍵はどちらも文字列リテラルなので、同じ .hlsl なら必ず同じ番地を指す。
			if (compiled.sourceRelativePath == sourceRelativePath && compiled.entryPointName == entryPointName)
			{
				return &compiled;
			}
		}

		return nullptr;
	}


	void ShaderHotReload::ClearChangedFlags()
	{
		for (uint32_t index = 0; index < MAX_WATCH_COUNT; ++index)
		{
			m_isWatchChanged[index] = false;
		}

		m_hasExtendedForFileAccess = false;
	}


	void ShaderHotReload::SetStatus(bool isSuccessful, const char* message)
	{
		m_status.wasLastAttemptSuccessful = isSuccessful;
		if (isSuccessful)
		{
			++m_status.successCount;
		}
		else
		{
			++m_status.failureCount;
		}

		// 長いエラー文は入るところまで。切れても先頭にファイル名と行番号があるので用は足りる。
		std::snprintf(
			m_status.lastMessage,
			ShaderReloadStatus::MAX_MESSAGE_LENGTH,
			"%s",
			message != nullptr ? message : ""
		);

		if (isSuccessful)
		{
			FANG_LOG_INFO(RHI, "シェーダーを作り直した: {}", m_status.lastMessage);
		}
		else
		{
			FANG_LOG_ERROR(RHI, "シェーダーの作り直しに失敗した: {}", m_status.lastMessage);
		}
	}

#endif
} // namespace fang::rhi
