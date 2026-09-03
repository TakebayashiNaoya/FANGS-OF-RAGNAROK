/**
 * @file RenderGraphTests.cpp
 * @brief RenderGraph の Compile のテスト。バリアが出る位置と回数、クリア指示の有無を確かめる。
 * @details GPU を作らずに済むよう Compile だけを対象にする。Execute はデバイスが要るのでここでは呼ばない。
 */
#include "RHI/RHITypes.h"
#include "Renderer/RenderGraph.h"
#include <doctest.h>


namespace
{
	/** @brief 記録関数を持たない、色だけ書くパスの宣言を作る。 */
	fang::RenderGraphPassDesc MakeColorPass(
		fang::RenderGraphResourceId colorTarget,
		fang::EnLoadOperation       loadOperation
	)
	{
		return fang::RenderGraphPassDesc{
			.name               = "Color",
			.colorTarget        = colorTarget,
			.colorLoadOperation = loadOperation,
		};
	}
} // namespace


TEST_CASE("バックバッファへ続けて書く 2 パスでは最初のパスの先頭にだけ遷移が出る")
{
	fang::RenderGraph graph;
	graph.Reset();

	const fang::RenderGraphResourceId backBuffer = graph.ImportBackBuffer();

	graph.AddPass(MakeColorPass(backBuffer, fang::EnLoadOperation::Clear));
	graph.AddPass(MakeColorPass(backBuffer, fang::EnLoadOperation::Load));
	graph.Compile();

	CHECK(graph.GetPassCount() == 2);

	const fang::CompiledRenderPass& firstPass = graph.GetCompiledPass(0);
	CHECK(firstPass.beginBarrierCount == 1);
	CHECK(firstPass.beginBarriers[0].resource.index == backBuffer.index);
	CHECK(firstPass.beginBarriers[0].before == fang::rhi::EnResourceState::Present);
	CHECK(firstPass.beginBarriers[0].after == fang::rhi::EnResourceState::RenderTarget);

	// 用途が変わらないので、続くパスにはバリアが 1 本も要らない。
	CHECK(graph.GetCompiledPass(1).beginBarrierCount == 0);
}


TEST_CASE("最終状態への遷移は最後に使うパスの末尾にだけ出る")
{
	fang::RenderGraph graph;
	graph.Reset();

	const fang::RenderGraphResourceId backBuffer = graph.ImportBackBuffer();

	graph.AddPass(MakeColorPass(backBuffer, fang::EnLoadOperation::Clear));
	graph.AddPass(MakeColorPass(backBuffer, fang::EnLoadOperation::Load));
	graph.Compile();

	CHECK(graph.GetCompiledPass(0).endBarrierCount == 0);

	const fang::CompiledRenderPass& lastPass = graph.GetCompiledPass(1);
	CHECK(lastPass.endBarrierCount == 1);
	CHECK(lastPass.endBarriers[0].resource.index == backBuffer.index);
	CHECK(lastPass.endBarriers[0].before == fang::rhi::EnResourceState::RenderTarget);
	CHECK(lastPass.endBarriers[0].after == fang::rhi::EnResourceState::Present);
}


TEST_CASE("深度は用途が変わらないのでバリアが 1 本も出ない")
{
	fang::RenderGraph graph;
	graph.Reset();

	const fang::RenderGraphResourceId backBuffer  = graph.ImportBackBuffer();
	const fang::RenderGraphResourceId depthBuffer = graph.ImportDepthBuffer();

	fang::RenderGraphPassDesc scenePass = MakeColorPass(backBuffer, fang::EnLoadOperation::Clear);
	scenePass.depthTarget               = depthBuffer;
	scenePass.depthLoadOperation        = fang::EnLoadOperation::Clear;

	graph.AddPass(scenePass);
	graph.AddPass(MakeColorPass(backBuffer, fang::EnLoadOperation::Load));
	graph.Compile();

	// 出るのはバックバッファの 2 本（先頭の RenderTarget と末尾の Present）だけ。
	const fang::CompiledRenderPass& firstPass = graph.GetCompiledPass(0);
	const fang::CompiledRenderPass& lastPass  = graph.GetCompiledPass(1);

	CHECK(firstPass.beginBarrierCount == 1);
	CHECK(firstPass.endBarrierCount == 0);
	CHECK(lastPass.beginBarrierCount == 0);
	CHECK(lastPass.endBarrierCount == 1);

	CHECK(firstPass.beginBarriers[0].resource.index == backBuffer.index);
	CHECK(lastPass.endBarriers[0].resource.index == backBuffer.index);
}


TEST_CASE("パスを 1 つも宣言しなければ Compile しても何も出ない")
{
	fang::RenderGraph graph;
	graph.Reset();

	const fang::RenderGraphResourceId backBuffer = graph.ImportBackBuffer();
	CHECK(backBuffer.IsValid());

	graph.Compile();

	CHECK(graph.GetPassCount() == 0);
	CHECK(graph.GetCommandLists().empty());
}


TEST_CASE("クリア指示は Clear を宣言したパスにだけ出る")
{
	fang::RenderGraph graph;
	graph.Reset();

	const fang::RenderGraphResourceId backBuffer  = graph.ImportBackBuffer();
	const fang::RenderGraphResourceId depthBuffer = graph.ImportDepthBuffer();

	constexpr fang::rhi::ClearColor clearColor{ .red = 0.25f, .green = 0.5f, .blue = 0.75f, .alpha = 1.0f };

	fang::RenderGraphPassDesc scenePass = MakeColorPass(backBuffer, fang::EnLoadOperation::Clear);
	scenePass.clearColor                = clearColor;
	scenePass.depthTarget               = depthBuffer;
	scenePass.depthLoadOperation        = fang::EnLoadOperation::Clear;

	graph.AddPass(scenePass);
	graph.AddPass(MakeColorPass(backBuffer, fang::EnLoadOperation::Load));
	graph.Compile();

	const fang::CompiledRenderPass& clearingPass = graph.GetCompiledPass(0);
	CHECK(clearingPass.isColorCleared);
	CHECK(clearingPass.isDepthCleared);
	CHECK(clearingPass.clearColor.red == doctest::Approx(clearColor.red));
	CHECK(clearingPass.clearColor.green == doctest::Approx(clearColor.green));
	CHECK(clearingPass.clearColor.blue == doctest::Approx(clearColor.blue));

	const fang::CompiledRenderPass& loadingPass = graph.GetCompiledPass(1);
	CHECK_FALSE(loadingPass.isColorCleared);
	CHECK_FALSE(loadingPass.isDepthCleared);
}
