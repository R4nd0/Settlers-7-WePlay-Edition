lab_resume_rng_test_clobber = true;
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRng01(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRng02(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRng06(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRng07(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRng08(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRng0A(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRng0B(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRng0E(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRng0F(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRng10(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRng13(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRng39(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRngC0(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRngC1(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRngC2(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRngC8(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRngC9(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
{
    for (uint32_t lab_input : {0u, 1u, 0x80000001u, 0xffffffffu}) {
        uint32_t lab_actual = 0, lab_flags = 0;
        alignas(16) unsigned char lab_before[512]{}, lab_after[512]{};
        SetLastError(4444); _fxsave(lab_before);
        TestLabResumeRngD0(lab_input, &lab_actual, &lab_flags);
        _fxsave(lab_after);
        check(GetLastError() == 4444 && lab_actual == lab_input * 0x6c078965u);
        const int64_t full = static_cast<int64_t>(static_cast<int32_t>(lab_input)) * 0x6c078965;
        const bool overflow = full < INT32_MIN || full > INT32_MAX;
        check((lab_flags & 0x801) == (overflow ? 0x801u : 0u));
        check(memcmp(lab_before, lab_after, 16) == 0 && memcmp(lab_before + 24, lab_after + 24, 8) == 0);
        check(memcmp(lab_before + 32, lab_after + 32, 256) == 0);
    }
}
lab_resume_rng_test_clobber = false;
