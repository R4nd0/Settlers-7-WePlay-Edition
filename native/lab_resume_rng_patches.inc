unsigned char LabResumeRngBytes[56][6]{};
const Patch LabResumeRngPatches[] = {
    {0x4F2206, {0x69,0xc0,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[0], false, 6},
    {0x4F22AB, {0x69,0xc0,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[1], false, 6},
    {0x530536, {0x69,0x02,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[2], false, 6},
    {0x55B0C2, {0x69,0x08,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[3], false, 6},
    {0x5733C0, {0x69,0x07,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[4], false, 6},
    {0x573401, {0x69,0x0f,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[5], false, 6},
    {0x57B4F4, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[6], false, 6},
    {0x5F1C43, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[7], false, 6},
    {0x63FC8B, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[8], false, 6},
    {0x63FD0E, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[9], false, 6},
    {0x6C1F49, {0x69,0x02,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[10], false, 6},
    {0x6C1FA1, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[11], false, 6},
    {0x6C20EE, {0x69,0x02,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[12], false, 6},
    {0x6C7954, {0x69,0x07,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[13], false, 6},
    {0x6CA1C5, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[14], false, 6},
    {0x6D0F86, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[15], false, 6},
    {0x6D4775, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[16], false, 6},
    {0x6D8261, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[17], false, 6},
    {0x710D82, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[18], false, 6},
    {0x72061B, {0x69,0x39,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[19], false, 6},
    {0x72F2FA, {0x69,0x0a,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[20], false, 6},
    {0x72F31E, {0x69,0xc1,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[21], false, 6},
    {0x72FE87, {0x69,0x0a,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[22], false, 6},
    {0x73C17C, {0x69,0x06,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[23], false, 6},
    {0x73C1C8, {0x69,0x0e,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[24], false, 6},
    {0x73C216, {0x69,0xc1,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[25], false, 6},
    {0x73C274, {0x69,0x06,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[26], false, 6},
    {0x73D07B, {0x69,0x07,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[27], false, 6},
    {0x73D0BF, {0x69,0x07,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[28], false, 6},
    {0x753C5E, {0x69,0x08,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[29], false, 6},
    {0x761373, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[30], false, 6},
    {0x793E5F, {0x69,0x08,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[31], false, 6},
    {0x7BEC1A, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[32], false, 6},
    {0x7BF360, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[33], false, 6},
    {0x7BF547, {0x69,0x02,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[34], false, 6},
    {0x7C294E, {0x69,0x13,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[35], false, 6},
    {0x7C29A8, {0x69,0xc2,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[36], false, 6},
    {0x7CCEB8, {0x69,0x0b,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[37], false, 6},
    {0x7CF0F8, {0x69,0x0b,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[38], false, 6},
    {0x7D1498, {0x69,0x0a,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[39], false, 6},
    {0x7E1A61, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[40], false, 6},
    {0x7E6C67, {0x69,0x10,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[41], false, 6},
    {0x7E6CDC, {0x69,0x10,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[42], false, 6},
    {0x7F4E38, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[43], false, 6},
    {0x7F5210, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[44], false, 6},
    {0x7F54BC, {0x69,0x01,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[45], false, 6},
    {0x829236, {0x69,0xc9,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[46], false, 6},
    {0x829266, {0x69,0xc1,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[47], false, 6},
    {0x83F124, {0x69,0xc9,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[48], false, 6},
    {0x83F130, {0x69,0xc9,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[49], false, 6},
    {0x83F1AA, {0x69,0xc1,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[50], false, 6},
    {0xC5605A, {0x69,0xd0,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[51], false, 6},
    {0xCD208F, {0x69,0xc8,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[52], false, 6},
    {0xCD98AA, {0x69,0xc8,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[53], false, 6},
    {0xCD99BA, {0x69,0xc8,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[54], false, 6},
    {0xCEACFD, {0x69,0xc1,0x65,0x89,0x07,0x6c}, LabResumeRngBytes[55], false, 6},
};
void InitializeLabResumeRngPatches() {
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC0) - (NativeVA(0x4F2206) + 5)); LabResumeRngBytes[0][0] = 0xe8; memcpy(LabResumeRngBytes[0] + 1, &relative, 4); LabResumeRngBytes[0][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC0) - (NativeVA(0x4F22AB) + 5)); LabResumeRngBytes[1][0] = 0xe8; memcpy(LabResumeRngBytes[1] + 1, &relative, 4); LabResumeRngBytes[1][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng02) - (NativeVA(0x530536) + 5)); LabResumeRngBytes[2][0] = 0xe8; memcpy(LabResumeRngBytes[2] + 1, &relative, 4); LabResumeRngBytes[2][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng08) - (NativeVA(0x55B0C2) + 5)); LabResumeRngBytes[3][0] = 0xe8; memcpy(LabResumeRngBytes[3] + 1, &relative, 4); LabResumeRngBytes[3][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng07) - (NativeVA(0x5733C0) + 5)); LabResumeRngBytes[4][0] = 0xe8; memcpy(LabResumeRngBytes[4] + 1, &relative, 4); LabResumeRngBytes[4][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng0F) - (NativeVA(0x573401) + 5)); LabResumeRngBytes[5][0] = 0xe8; memcpy(LabResumeRngBytes[5] + 1, &relative, 4); LabResumeRngBytes[5][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x57B4F4) + 5)); LabResumeRngBytes[6][0] = 0xe8; memcpy(LabResumeRngBytes[6] + 1, &relative, 4); LabResumeRngBytes[6][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x5F1C43) + 5)); LabResumeRngBytes[7][0] = 0xe8; memcpy(LabResumeRngBytes[7] + 1, &relative, 4); LabResumeRngBytes[7][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x63FC8B) + 5)); LabResumeRngBytes[8][0] = 0xe8; memcpy(LabResumeRngBytes[8] + 1, &relative, 4); LabResumeRngBytes[8][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x63FD0E) + 5)); LabResumeRngBytes[9][0] = 0xe8; memcpy(LabResumeRngBytes[9] + 1, &relative, 4); LabResumeRngBytes[9][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng02) - (NativeVA(0x6C1F49) + 5)); LabResumeRngBytes[10][0] = 0xe8; memcpy(LabResumeRngBytes[10] + 1, &relative, 4); LabResumeRngBytes[10][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x6C1FA1) + 5)); LabResumeRngBytes[11][0] = 0xe8; memcpy(LabResumeRngBytes[11] + 1, &relative, 4); LabResumeRngBytes[11][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng02) - (NativeVA(0x6C20EE) + 5)); LabResumeRngBytes[12][0] = 0xe8; memcpy(LabResumeRngBytes[12] + 1, &relative, 4); LabResumeRngBytes[12][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng07) - (NativeVA(0x6C7954) + 5)); LabResumeRngBytes[13][0] = 0xe8; memcpy(LabResumeRngBytes[13] + 1, &relative, 4); LabResumeRngBytes[13][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x6CA1C5) + 5)); LabResumeRngBytes[14][0] = 0xe8; memcpy(LabResumeRngBytes[14] + 1, &relative, 4); LabResumeRngBytes[14][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x6D0F86) + 5)); LabResumeRngBytes[15][0] = 0xe8; memcpy(LabResumeRngBytes[15] + 1, &relative, 4); LabResumeRngBytes[15][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x6D4775) + 5)); LabResumeRngBytes[16][0] = 0xe8; memcpy(LabResumeRngBytes[16] + 1, &relative, 4); LabResumeRngBytes[16][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x6D8261) + 5)); LabResumeRngBytes[17][0] = 0xe8; memcpy(LabResumeRngBytes[17] + 1, &relative, 4); LabResumeRngBytes[17][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x710D82) + 5)); LabResumeRngBytes[18][0] = 0xe8; memcpy(LabResumeRngBytes[18] + 1, &relative, 4); LabResumeRngBytes[18][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng39) - (NativeVA(0x72061B) + 5)); LabResumeRngBytes[19][0] = 0xe8; memcpy(LabResumeRngBytes[19] + 1, &relative, 4); LabResumeRngBytes[19][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng0A) - (NativeVA(0x72F2FA) + 5)); LabResumeRngBytes[20][0] = 0xe8; memcpy(LabResumeRngBytes[20] + 1, &relative, 4); LabResumeRngBytes[20][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC1) - (NativeVA(0x72F31E) + 5)); LabResumeRngBytes[21][0] = 0xe8; memcpy(LabResumeRngBytes[21] + 1, &relative, 4); LabResumeRngBytes[21][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng0A) - (NativeVA(0x72FE87) + 5)); LabResumeRngBytes[22][0] = 0xe8; memcpy(LabResumeRngBytes[22] + 1, &relative, 4); LabResumeRngBytes[22][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng06) - (NativeVA(0x73C17C) + 5)); LabResumeRngBytes[23][0] = 0xe8; memcpy(LabResumeRngBytes[23] + 1, &relative, 4); LabResumeRngBytes[23][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng0E) - (NativeVA(0x73C1C8) + 5)); LabResumeRngBytes[24][0] = 0xe8; memcpy(LabResumeRngBytes[24] + 1, &relative, 4); LabResumeRngBytes[24][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC1) - (NativeVA(0x73C216) + 5)); LabResumeRngBytes[25][0] = 0xe8; memcpy(LabResumeRngBytes[25] + 1, &relative, 4); LabResumeRngBytes[25][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng06) - (NativeVA(0x73C274) + 5)); LabResumeRngBytes[26][0] = 0xe8; memcpy(LabResumeRngBytes[26] + 1, &relative, 4); LabResumeRngBytes[26][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng07) - (NativeVA(0x73D07B) + 5)); LabResumeRngBytes[27][0] = 0xe8; memcpy(LabResumeRngBytes[27] + 1, &relative, 4); LabResumeRngBytes[27][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng07) - (NativeVA(0x73D0BF) + 5)); LabResumeRngBytes[28][0] = 0xe8; memcpy(LabResumeRngBytes[28] + 1, &relative, 4); LabResumeRngBytes[28][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng08) - (NativeVA(0x753C5E) + 5)); LabResumeRngBytes[29][0] = 0xe8; memcpy(LabResumeRngBytes[29] + 1, &relative, 4); LabResumeRngBytes[29][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x761373) + 5)); LabResumeRngBytes[30][0] = 0xe8; memcpy(LabResumeRngBytes[30] + 1, &relative, 4); LabResumeRngBytes[30][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng08) - (NativeVA(0x793E5F) + 5)); LabResumeRngBytes[31][0] = 0xe8; memcpy(LabResumeRngBytes[31] + 1, &relative, 4); LabResumeRngBytes[31][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x7BEC1A) + 5)); LabResumeRngBytes[32][0] = 0xe8; memcpy(LabResumeRngBytes[32] + 1, &relative, 4); LabResumeRngBytes[32][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x7BF360) + 5)); LabResumeRngBytes[33][0] = 0xe8; memcpy(LabResumeRngBytes[33] + 1, &relative, 4); LabResumeRngBytes[33][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng02) - (NativeVA(0x7BF547) + 5)); LabResumeRngBytes[34][0] = 0xe8; memcpy(LabResumeRngBytes[34] + 1, &relative, 4); LabResumeRngBytes[34][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng13) - (NativeVA(0x7C294E) + 5)); LabResumeRngBytes[35][0] = 0xe8; memcpy(LabResumeRngBytes[35] + 1, &relative, 4); LabResumeRngBytes[35][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC2) - (NativeVA(0x7C29A8) + 5)); LabResumeRngBytes[36][0] = 0xe8; memcpy(LabResumeRngBytes[36] + 1, &relative, 4); LabResumeRngBytes[36][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng0B) - (NativeVA(0x7CCEB8) + 5)); LabResumeRngBytes[37][0] = 0xe8; memcpy(LabResumeRngBytes[37] + 1, &relative, 4); LabResumeRngBytes[37][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng0B) - (NativeVA(0x7CF0F8) + 5)); LabResumeRngBytes[38][0] = 0xe8; memcpy(LabResumeRngBytes[38] + 1, &relative, 4); LabResumeRngBytes[38][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng0A) - (NativeVA(0x7D1498) + 5)); LabResumeRngBytes[39][0] = 0xe8; memcpy(LabResumeRngBytes[39] + 1, &relative, 4); LabResumeRngBytes[39][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x7E1A61) + 5)); LabResumeRngBytes[40][0] = 0xe8; memcpy(LabResumeRngBytes[40] + 1, &relative, 4); LabResumeRngBytes[40][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng10) - (NativeVA(0x7E6C67) + 5)); LabResumeRngBytes[41][0] = 0xe8; memcpy(LabResumeRngBytes[41] + 1, &relative, 4); LabResumeRngBytes[41][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng10) - (NativeVA(0x7E6CDC) + 5)); LabResumeRngBytes[42][0] = 0xe8; memcpy(LabResumeRngBytes[42] + 1, &relative, 4); LabResumeRngBytes[42][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x7F4E38) + 5)); LabResumeRngBytes[43][0] = 0xe8; memcpy(LabResumeRngBytes[43] + 1, &relative, 4); LabResumeRngBytes[43][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x7F5210) + 5)); LabResumeRngBytes[44][0] = 0xe8; memcpy(LabResumeRngBytes[44] + 1, &relative, 4); LabResumeRngBytes[44][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRng01) - (NativeVA(0x7F54BC) + 5)); LabResumeRngBytes[45][0] = 0xe8; memcpy(LabResumeRngBytes[45] + 1, &relative, 4); LabResumeRngBytes[45][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC9) - (NativeVA(0x829236) + 5)); LabResumeRngBytes[46][0] = 0xe8; memcpy(LabResumeRngBytes[46] + 1, &relative, 4); LabResumeRngBytes[46][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC1) - (NativeVA(0x829266) + 5)); LabResumeRngBytes[47][0] = 0xe8; memcpy(LabResumeRngBytes[47] + 1, &relative, 4); LabResumeRngBytes[47][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC9) - (NativeVA(0x83F124) + 5)); LabResumeRngBytes[48][0] = 0xe8; memcpy(LabResumeRngBytes[48] + 1, &relative, 4); LabResumeRngBytes[48][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC9) - (NativeVA(0x83F130) + 5)); LabResumeRngBytes[49][0] = 0xe8; memcpy(LabResumeRngBytes[49] + 1, &relative, 4); LabResumeRngBytes[49][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC1) - (NativeVA(0x83F1AA) + 5)); LabResumeRngBytes[50][0] = 0xe8; memcpy(LabResumeRngBytes[50] + 1, &relative, 4); LabResumeRngBytes[50][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngD0) - (NativeVA(0xC5605A) + 5)); LabResumeRngBytes[51][0] = 0xe8; memcpy(LabResumeRngBytes[51] + 1, &relative, 4); LabResumeRngBytes[51][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC8) - (NativeVA(0xCD208F) + 5)); LabResumeRngBytes[52][0] = 0xe8; memcpy(LabResumeRngBytes[52] + 1, &relative, 4); LabResumeRngBytes[52][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC8) - (NativeVA(0xCD98AA) + 5)); LabResumeRngBytes[53][0] = 0xe8; memcpy(LabResumeRngBytes[53] + 1, &relative, 4); LabResumeRngBytes[53][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC8) - (NativeVA(0xCD99BA) + 5)); LabResumeRngBytes[54][0] = 0xe8; memcpy(LabResumeRngBytes[54] + 1, &relative, 4); LabResumeRngBytes[54][5] = 0x90; }
    { const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LabResumeRngC1) - (NativeVA(0xCEACFD) + 5)); LabResumeRngBytes[55][0] = 0xe8; memcpy(LabResumeRngBytes[55] + 1, &relative, 4); LabResumeRngBytes[55][5] = 0x90; }
}
