# GetTickCountImpl
An internal implementation for GetTickCount &amp; GetTickCount64

# Implementation details
The _GetTickCount_ & _GetTickCount64_ functions utilizes the `TickCount` and the `TickCountMultiplier` field inside the _KUSER_SHARED_DATA_. 

In this brief explanation I'll demonstrate how some of the internal code behind GetTickCount function works.

The GetTickCount function is composed out of two variables:

1) `TickCount` located at volatile address _0x7FFE0320_.
1) `TickCountMultiplier` located at volatile address _0x7FFE0004_.

As the name of `TickCountMultiplier` describes, it is a multiplier which is used to multiply `TickCount` that is constantly incrementing by a rate of 1/ms. That's it. That's the entire trick behind it. However the interesting things happen under the hood. 

For example, how is the multiplier calculated, or why even. That's what I wanna cover in the next few words. So, in order to obtain the knowledge of how things work deeply inside the kernel, we have to take a deep dive into the both 32-bit & 64-bit version of `kernel32.dll` module. 

The 32-bit version is located under _SysWOW64/_ and the 64-bit version under _system32/_. Why is the 32-bit version located inside a folder that has 64 in it and the 64-bit version inside a folder that has 32 in it is for another topic. In short, _system32/_ stores 64-bit modules and _SysWOW64/_ does the opposite.

Now when we know where the cool stuff is happening, we can examine the actual code. But before we do that, let's see the IDA's pseudocode for these two functions.

```c++
DWORD __stdcall GetTickCountKernel32()
{
  return MEMORY[0x7FFE0320] * (uint64_t)MEMORY[0x7FFE0004] >> 24;
}

ULONGLONG __stdcall GetTickCount64Kernel32()
{
  return ((uint64_t)MEMORY[0x7FFE0004] << 32) * (uint64_t)(MEMORY[0x7FFE0320] << 8) >> 64;
}
```

As we can see, the memory location that both functions are using refers to _KUSER_SHARED_DATA_ structure located at fixed address _0x7FFFE0000_. By the way, these two are for the 64-bit version of windows (_system32/kernel32.dll_). If we simplify the code, we will get something like this:

```c++
DWORD __stdcall GetTickCountKernel32()
{
  return KSharedUserData->TickCountQuad * (uint64_t)TickCountPtr->TickCountMultiplier >> 24;
}

ULONGLONG __stdcall GetTickCount64Kernel32()
{
  return ((uint64_t)TickCountPtr->TickCountMultiplier << 32) * (uint64_t)( KSharedUserData->TickCountQuad << 8) >> 64;
}
```

Now it atleast make more sence, but anyways, this is the implementation of _GetTickCount_ routines. Now we can finally start making sence of how does this work.

## TickCountMultiplier

Let's start with `TickCountMultiplier`. So, this field from _KUSER_SHARED_DATA_ is constant, it doesn't change, ever. And because of that, and due to optimizations, it is pre-calculated inside the kernel at startup. To be more exact, inside `InitBootProcessor` routine that is called at the very begining of windows startup inside `ntoskrnl.exe`.

```c++
ExpTickCountMultiplier = ExComputeTickCountMultiplier();
SharedUserData->TickCountMultiplier = ExpTickCountMultiplier;
```

As we can see, it is all happening inside `ExComputeTickCountMultiplier` routine. The `KeMaximumIncrement` argument passed is architecture-dependent, but the general description of it says, "clock increment value in 100ns units". This is the base variable for the `TickCountMultiplier`. 

Anyway, let's dive into that function. The function itself is simple and it it's body can be divided into three stages:

1) From the `KeMaximumIncrement` compute the 8-bit integer part.
2) From the `KeMaximumIncrement` compute the 24-bit fraction part.
3) From the integer and fraction part compute the final result.

I will now cover these three steps in detail. Starting with a general description of what happens with `KeMaximumIncrement` as input. 

The `KeMaximumIncrement` is a 32-bit integer that has integer and fraction part. The integer part covers 8-bits and the fraction remaining 24-bits of space. 

As the first step, the integer part is computed by dividing `KeMaximumIncrement` by 10,000 (100ns * 10,000ns is 1ms). Using this formula, it is clear that we're getting the millisecond part out of the constant. This is the same as if a binary number would be right-shifted, except that there we don't have base 2, but base 10. Let's store that into variable _IntegerPart_.

```c++
ULONG IntegerPart = KeMaximumIncrement / 10000;
```

As the second step, the remainder is computed. With some imagination in mind, this is how it can be done. We will store it inside variable _Remainder_.

```c++
ULONG Remainder = KeMaximumIncrement - (IntegerPart * 10000);
```

So now we have the integer part and the remainder. You can think of this as a float value, where the base is _IntegerPart_ and the irrational part is _Remainder_, with an only difference that the float is represented by an integer in this case.

Next up is a algorithm that calculates the binary fraction from the _Remainder_. The rules are following, multiply the fractioned remainder by two (left shift) and if the result is bigger than one, store 1 and substract 1 from the result, store 0 otherwise. Repeat 24 times in this case, since we're calculating up to 24-bit precision. The result of stored ones and zeros then pack together into one sequence.

As a demonstration, let's take decimal number 6789 as _Remainder_ and make fraction from it - 0.6789.

```
0.6789 * 2   =   1.3578   1    |
0.3578 * 2   =   0.7156   0    |
0.7156 * 2   =   1.4312   1    |
0.4312 * 2   =   0.8624   0    |
0.8624 * 2   =   1.7248   1    |
0.7248 * 2   =   1.4496   1    |
0.4496 * 2   =   0.8992   0    |
0.8992 * 2   =   1.7984   1    |
0.7984 * 2   =   1.5968   1    V
24 times total...

The result would be:
0,6789 in decimal is 0,101011011... in binary
```

The C pseudocode for this algorithm would be:

```c++
	ULONG FractionPart = 0;
	for (int n = 0; n < 24; n++)
	{
		FractionPart <<= 1; // Move to next bit (* 2)
		Remainder <<= 1;
		if (Remainder >= 10000)
		{
			// In this case the base is 10000, so we substract 10000 instead of 1.
			Remainder -= 10000;
			FractionPart |= 1; // Add 1 bit. Otherwise none (0) would be added.
		}
	}
```

And that's the whole magic behind this. In the final step we take the calculated _FractionPart_ and we add it into the _IntegerPart_ using some bitwise operations. Since the final result is 32-bit, the _IntegerPart_ is 8-bit and the _FractionPart_ is the remaining 24-bits, we do following trick:

```c++
ULONG Result = (IntegerPart << 24) | FractionPart; 
```

This combines up the _IntegerPart_ and the fraction part into one final integer. And there you have it. This is how the _TickCountMultiplier_ is calculated deeply inside the kernel.

## TickCount

Now it's time to cover the other important side of _GetTickCount_, and that is `TickCount`. TickCount is a type of union, that means that it can be more types at once.

```c++
typedef struct _KSYSTEM_TIME
{
	ULONG LowPart;
	LONG High1Time;
	LONG High2Time;
} KSYSTEM_TIME, * PKSYSTEM_TIME;

// Union inside KUSER_SHARED_DATA to divide between kernel 
// timming data structures into x64 & x86 modes.
union
{
	KSYSTEM_TIME TickCount;
	UINT64 TickCountQuad;
};
```

The `TickCount` is calculated very deeply inside the `ntoskrnl.exe` in `KiCalibrateTimeAdjustment` routine. I'm not gonna cover deeply how exacly windows do the timing stuff, since that part of kernel is heavily undocumented, thus studying it makes it pretty hard. 

To wrap it up, the `TickCount` is just an increment by a rate of 1 per millisecond and multiplying it by the _TickCountMultiplier_ gives the final result of _GetTickCount_.

