// PA250InputBuffer.h — buffered player input (M3). A press that cannot be acted on at once is kept
// for its window (battle_feel.json input.buffer_seconds) and acted on the moment it can be; older
// than the window it is dropped. Pure: the caller gives the time. Shared vectors:
// protocol/test_vectors/anim/input_buffer_*.json.
#pragma once

#include "CoreMinimal.h"

enum class EPA250BufferedAction : uint8
{
	Fire,
	Reload,
	Vault,
	Order,
	Thrust,
	Count
};

struct FPA250InputBuffer
{
	/** Remembers a press (a later press replaces an earlier one). Payload: the order's number. */
	void Press(EPA250BufferedAction Action, double Now, int32 InPayload = 0)
	{
		PressedAt[Index(Action)] = Now;
		Payload[Index(Action)] = InPayload;
	}

	/** Is a press held and still inside its window? A press older than the window is dropped here. */
	bool Live(EPA250BufferedAction Action, double Now, float WindowSeconds)
	{
		double& At = PressedAt[Index(Action)];
		if (At < 0.0) return false;
		if (Now - At > static_cast<double>(WindowSeconds) + 1.e-6)
		{
			At = -1.0;
			return false;
		}
		return true;
	}

	/** The press has been acted on. */
	void Spend(EPA250BufferedAction Action) { PressedAt[Index(Action)] = -1.0; }

	/** The action can be done now: true (once) when a press inside its window is held. */
	bool Consume(EPA250BufferedAction Action, double Now, float WindowSeconds, int32* OutPayload = nullptr)
	{
		if (!Live(Action, Now, WindowSeconds)) return false;
		if (OutPayload) *OutPayload = Payload[Index(Action)];
		Spend(Action);
		return true;
	}

	bool Pending(EPA250BufferedAction Action) const { return PressedAt[Index(Action)] >= 0.0; }
	int32 GetPayload(EPA250BufferedAction Action) const { return Payload[Index(Action)]; }

private:
	static int32 Index(EPA250BufferedAction Action) { return static_cast<int32>(Action); }
	double PressedAt[static_cast<int32>(EPA250BufferedAction::Count)] = {-1.0, -1.0, -1.0, -1.0, -1.0};
	int32 Payload[static_cast<int32>(EPA250BufferedAction::Count)] = {0, 0, 0, 0, 0};
};
