/**
 * @file DdsImage.h
 * @brief DDS ファイルの解析。ミップごとの範囲を返す。
 */
#pragma once

#include "Core/CoreMacros.h"
#include "RHI/RHITypes.h"
#include <cstdint>
#include <span>
#include <vector>


namespace fang
{
	/**
	 * @brief DDS を読み、GPU へ渡せる形（形式とミップごとの範囲）にして持つ。
	 * @details GPU リソースは作らない。GltfMesh と同じで、CPU 側の情報を返すところまでがこのクラスの仕事。
	 *          読めるのは DX10 拡張ヘッダ付きの 2D テクスチャだけ（texconv の -dx10 出力。BC7 は常にこの形）。
	 *          それ以外の DDS はエラーにして読まない ➡ 黙って化けた絵を出さない。
	 * @threading メインスレッドのみ。
	 */
	class DdsImage
	{
	public:
		FANG_NON_COPYABLE(DdsImage);

		DdsImage() = default;

		/**
		 * @brief DDS ファイルを読んで解析する。
		 * @param filePath 読み込む .dds の絶対パス。
		 * @return 失敗したら false。理由はログに出す。失敗しても落ちず中身は空のままになるので、
		 *         呼び出し側はテクスチャをあきらめて先へ進めばよい。
		 */
		[[nodiscard]] bool Load(const char* filePath);

		/**
		 * @brief メモリ上の DDS を解析する。中身は自分の側へ写す。
		 * @details ファイルを介さずに確かめられるよう、テストからも呼ぶ。
		 * @return 失敗したら false。
		 */
		[[nodiscard]] bool LoadFromMemory(std::span<const uint8_t> bytes);

		/** @brief ピクセル形式。読めていなければ既定値のまま意味を持たない。 */
		[[nodiscard]] FANG_FORCEINLINE rhi::EnTextureFormat GetFormat() const { return m_format; }

		/**
		 * @brief ミップごとの範囲。0 番が最大。読めていなければ空。
		 * @details 中身はこのオブジェクトが持つ ➡ 返した span はこのオブジェクトより長生きさせない。
		 */
		[[nodiscard]] FANG_FORCEINLINE std::span<const rhi::TextureMipLevel> GetMipLevels() const
		{
			return m_mipLevels;
		}


	private:
		/** @brief 読み込んだバイト列を解析して m_mipLevels を組み立てる。 */
		[[nodiscard]] bool Parse();

		/** @brief 中身を全部空にする。読み込みに失敗したとき中途半端な中身を残さないため。 */
		void Clear();

		std::vector<uint8_t>              m_fileBytes; /**< ファイルの中身。ミップの範囲はここを指す。 */
		std::vector<rhi::TextureMipLevel> m_mipLevels;

		rhi::EnTextureFormat m_format = rhi::EnTextureFormat::RGBA8;
	};
} // namespace fang
