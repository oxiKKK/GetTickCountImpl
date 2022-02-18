#include <iostream>
#include <Windows.h>

typedef struct _KSYSTEM_TIME
{
	ULONG LowPart;
	LONG High1Time;
	LONG High2Time;
} KSYSTEM_TIME, * PKSYSTEM_TIME;

union TICK_COUNT_UNION
{
	KSYSTEM_TIME TickCount;
	UINT64 TickCountQuad;
};

volatile auto TickCountPtr = reinterpret_cast<TICK_COUNT_UNION* volatile>(0x7FFE0320);
volatile auto TickCountMultiplierPtr = reinterpret_cast<uint32_t* volatile>(0x7FFE0004);

uint32_t GetTickCountImpl()
{
	return TickCountPtr->TickCountQuad * static_cast<uint64_t>(*TickCountMultiplierPtr) >> 24;
}

static uint64_t GetTickCount64Impl()
{
#ifdef _WIN64
	return TickCountPtr->TickCountQuad * static_cast<uint64_t>(*TickCountMultiplierPtr) >> 24;
#else
	while (TickCountPtr->TickCount.High1Time != TickCountPtr->TickCount.High2Time)
		YieldProcessor();
	return (*TickCountMultiplierPtr * static_cast<uint64_t>(TickCountPtr->TickCount.High1Time) << 8) +
		(*TickCountMultiplierPtr * static_cast<uint64_t>(TickCountPtr->TickCount.LowPart) >> 24);
#endif
}

int main()
{
	while (true)
	{
		printf("%10lu %10lu\n", GetTickCount(), GetTickCountImpl());
		printf("%10llu %10llu\n", GetTickCount64(), GetTickCount64Impl());
		printf("\n");
	}
}