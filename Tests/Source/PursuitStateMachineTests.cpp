/**
 * @file PursuitStateMachineTests.cpp
 * @brief PursuitStateMachine のテスト。待機・追跡・見失いの遷移と、間合いでの停止・重なりでの安定を確かめる。
 */
#include "AI/AI.h"
#include "Core/Math/MathConstants.h"
#include "Core/Math/Vector3.h"
#include <doctest.h>
#include <cmath>


namespace
{
	/** @brief 成分ごとに近いことを見る。 */
	void CheckVector3(const fang::Vector3& actual, const fang::Vector3& expected)
	{
		CHECK(actual.x == doctest::Approx(expected.x));
		CHECK(actual.y == doctest::Approx(expected.y));
		CHECK(actual.z == doctest::Approx(expected.z));
	}
} // namespace


TEST_CASE("待機は見えなければ動かない")
{
	fang::AgentBlackboard blackboard{};
	fang::EnPursuitState  state = fang::EnPursuitState::Idle;

	const fang::MoveIntent intent =
		fang::StepPursuit(fang::PursuitParameter{}, blackboard, fang::Vector3{}, 1.0f / 60.0f, &state);

	CHECK(state == fang::EnPursuitState::Idle);
	CheckVector3(intent.desiredDelta, fang::Vector3{});
	CHECK_FALSE(intent.wantsToTurn);
}


TEST_CASE("見えると追跡へ移り、距離が縮む")
{
	const fang::PursuitParameter parameter{};

	fang::AgentBlackboard blackboard{};
	blackboard.isTargetVisible        = true;
	blackboard.hasLastSeenPosition    = true;
	blackboard.lastSeenTargetPosition = fang::Vector3{ 1000.0f, 0.0f, 0.0f };

	fang::EnPursuitState state    = fang::EnPursuitState::Idle;
	fang::Vector3        position = fang::Vector3{};

	float previousDistance = 1000.0f;
	for (int step = 0; step < 30; ++step)
	{
		const fang::MoveIntent intent = fang::StepPursuit(parameter, blackboard, position, 1.0f / 60.0f, &state);
		position += intent.desiredDelta;

		const float currentDistance = 1000.0f - position.x;
		CHECK(currentDistance <= previousDistance + 0.001f);
		previousDistance = currentDistance;
	}

	CHECK(state == fang::EnPursuitState::Chase);
	CHECK(previousDistance < 1000.0f);
}


TEST_CASE("間合いまで詰めたら止まり、押し合いを続けない")
{
	const fang::PursuitParameter parameter{};

	fang::AgentBlackboard blackboard{};
	blackboard.isTargetVisible        = true;
	blackboard.hasLastSeenPosition    = true;
	blackboard.lastSeenTargetPosition = fang::Vector3{ 1000.0f, 0.0f, 0.0f };

	fang::EnPursuitState state    = fang::EnPursuitState::Idle;
	fang::Vector3        position = fang::Vector3{};

	// 十分な時間をかけて間合いまで詰める。
	for (int step = 0; step < 600; ++step)
	{
		const fang::MoveIntent intent = fang::StepPursuit(parameter, blackboard, position, 1.0f / 60.0f, &state);
		position += intent.desiredDelta;
	}

	const float distanceAfterApproach = 1000.0f - position.x;
	CHECK(distanceAfterApproach <= parameter.stopDistanceCentimeters + 0.5f);

	// 間合いに入った後は、呼び続けても行き過ぎない（振動しない）。
	for (int step = 0; step < 30; ++step)
	{
		const fang::MoveIntent intent = fang::StepPursuit(parameter, blackboard, position, 1.0f / 60.0f, &state);
		position += intent.desiredDelta;

		CHECK((1000.0f - position.x) >= parameter.stopDistanceCentimeters - 0.5f);
	}
}


TEST_CASE("間合いで止まっていても、横へ回られると向きだけ追いかける")
{
	const fang::PursuitParameter parameter{};

	fang::AgentBlackboard blackboard{};
	blackboard.isTargetVisible     = true;
	blackboard.hasLastSeenPosition = true;

	// 間合い(stopDistance)ちょうどの距離に静止 ➡ desiredDelta は 0 のまま、向きだけ変わるはず。
	blackboard.lastSeenTargetPosition = fang::Vector3{ parameter.stopDistanceCentimeters, 0.0f, 0.0f };

	fang::EnPursuitState   state = fang::EnPursuitState::Chase;
	const fang::MoveIntent frontIntent =
		fang::StepPursuit(parameter, blackboard, fang::Vector3{}, 1.0f / 60.0f, &state);
	CheckVector3(frontIntent.desiredDelta, fang::Vector3{});
	CHECK(frontIntent.wantsToTurn);
	CHECK(frontIntent.facingRadians == doctest::Approx(0.0f));

	// 横へ回られた(Z方向)後も、動かないまま向きだけ追従する。
	blackboard.lastSeenTargetPosition = fang::Vector3{ 0.0f, 0.0f, parameter.stopDistanceCentimeters };
	const fang::MoveIntent sideIntent = fang::StepPursuit(parameter, blackboard, fang::Vector3{}, 1.0f / 60.0f, &state);
	CheckVector3(sideIntent.desiredDelta, fang::Vector3{});
	CHECK(sideIntent.wantsToTurn);
	CHECK(sideIntent.facingRadians == doctest::Approx(fang::PI * 0.5f));
}


TEST_CASE("見失うと最後に見た位置まで進み、追跡から見失いへ移る")
{
	const fang::PursuitParameter parameter{};

	fang::AgentBlackboard blackboard{};
	blackboard.isTargetVisible        = false;
	blackboard.hasLastSeenPosition    = true;
	blackboard.lastSeenTargetPosition = fang::Vector3{ 500.0f, 0.0f, 0.0f };
	blackboard.secondsSinceLastSeen   = 0.1f;

	fang::EnPursuitState state = fang::EnPursuitState::Chase;

	const fang::MoveIntent intent = fang::StepPursuit(parameter, blackboard, fang::Vector3{}, 1.0f / 60.0f, &state);

	CHECK(state == fang::EnPursuitState::Search);
	CHECK(intent.desiredDelta.x > 0.0f);
	CHECK(intent.wantsToTurn);
}


TEST_CASE("最後に見た位置へ着くと待機に戻る")
{
	const fang::PursuitParameter parameter{};

	fang::AgentBlackboard blackboard{};
	blackboard.isTargetVisible        = false;
	blackboard.hasLastSeenPosition    = true;
	blackboard.lastSeenTargetPosition = fang::Vector3{ 50.0f, 0.0f, 0.0f }; // arriveRadius(120) の内側。
	blackboard.secondsSinceLastSeen   = 0.1f;

	fang::EnPursuitState state = fang::EnPursuitState::Search;

	const fang::MoveIntent intent = fang::StepPursuit(parameter, blackboard, fang::Vector3{}, 1.0f / 60.0f, &state);

	CHECK(state == fang::EnPursuitState::Idle);
	CheckVector3(intent.desiredDelta, fang::Vector3{});
}


TEST_CASE("見失ってから一定時間で、着いていなくても待機に戻る")
{
	const fang::PursuitParameter parameter{};

	fang::AgentBlackboard blackboard{};
	blackboard.isTargetVisible        = false;
	blackboard.hasLastSeenPosition    = true;
	blackboard.lastSeenTargetPosition = fang::Vector3{ 5000.0f, 0.0f, 0.0f }; // arriveRadius よりずっと遠い。
	blackboard.secondsSinceLastSeen   = parameter.giveUpSeconds;              // 猶予をちょうど超えた。

	fang::EnPursuitState state = fang::EnPursuitState::Search;

	const fang::MoveIntent intent = fang::StepPursuit(parameter, blackboard, fang::Vector3{}, 1.0f / 60.0f, &state);

	CHECK(state == fang::EnPursuitState::Idle);
	CheckVector3(intent.desiredDelta, fang::Vector3{});
}


TEST_CASE("重なった位置で回しても振動せず NaN を出さない")
{
	const fang::PursuitParameter parameter{};

	fang::AgentBlackboard blackboard{};
	blackboard.isTargetVisible        = true;
	blackboard.hasLastSeenPosition    = true;
	blackboard.lastSeenTargetPosition = fang::Vector3{};

	fang::EnPursuitState state    = fang::EnPursuitState::Chase;
	const fang::Vector3  position = fang::Vector3{};

	for (int step = 0; step < 100; ++step)
	{
		const fang::MoveIntent intent = fang::StepPursuit(parameter, blackboard, position, 1.0f / 60.0f, &state);

		CHECK(std::isfinite(intent.desiredDelta.x));
		CHECK(std::isfinite(intent.desiredDelta.y));
		CHECK(std::isfinite(intent.desiredDelta.z));
		CheckVector3(intent.desiredDelta, fang::Vector3{});
	}
}
