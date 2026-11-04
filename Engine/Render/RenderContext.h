/**
 * @file RenderContext.h
 * @brief レンダーコンテキストクラス
 */
#pragma once


/**
 * @brief DeviceなどGPUを動かすためのオブジェクトを持ち、GPUに命令を渡して画面に出すクラス
 * @details シーンとビューはRendererが持ち、このクラスはシーンとビューを知らない。
 *          Rendererに入れると、シーンを足すときにGPUの部分をまた外へ出すことになるため層を分ける。
 */
class RenderContext
{
};
