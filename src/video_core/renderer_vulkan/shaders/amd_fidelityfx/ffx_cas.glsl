// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Based on AMD FidelityFX CAS shader code

#if CAS_SIMPLIFIED
void CasFilter(
    out AF1 pixR,
    out AF1 pixG,
    out AF1 pixB,
    ASU2 ip,
    AF2 con0,
    AF1 sharpness,
    AF1 inputViewInPixelsX,
    AF1 inputViewInPixelsY,
    AF1 inputOffsetInPixelsX,
    AF1 inputOffsetInPixelsY,
    texture2D tex)
{
    // Fetch the 4x4 neighborhood.
    CAS_SIMPLIFIED_OUTPUT n[4][4];
    for (ASU1 i = 0; i < 4; ++i) {
        for (ASU1 j = 0; j < 4; ++j) {
            ASU2 ij = ip + ASU2(-1 + j, -1 + i);
            n[i][j] = CAS_SIMPLIFIED_OUTPUT(texelFetch(tex, ij, 0).rgb);
        }
    }

    // Sharpening algorithm
    AF1 minR = min(min(n[0][1].x, n[1][0].x), min(n[1][2].x, n[2][1].x));
    AF1 maxR = max(max(n[0][1].x, n[1][0].x), max(n[1][2].x, n[2][1].x));
    AF1 minG = min(min(n[0][1].y, n[1][0].y), min(n[1][2].y, n[2][1].y));
    AF1 maxG = max(max(n[0][1].y, n[1][0].y), max(n[1][2].y, n[2][1].y));
    AF1 minB = min(min(n[0][1].z, n[1][0].z), min(n[1][2].z, n[2][1].z));
    AF1 maxB = max(max(n[0][1].z, n[1][0].z), max(n[1][2].z, n[2][1].z));

    AF1 sharpenR = saturate(abs(n[1][1].x - 0.5 * maxR - 0.5 * minR) * sharpness);
    AF1 sharpenG = saturate(abs(n[1][1].y - 0.5 * maxG - 0.5 * minG) * sharpness);
    AF1 sharpenB = saturate(abs(n[1][1].z - 0.5 * maxB - 0.5 * minB) * sharpness);

    pixR = lerp(n[1][1].x, clamp(n[1][1].x, minR, maxR), sharpenR);
    pixG = lerp(n[1][1].y, clamp(n[1][1].y, minG, maxG), sharpenG);
    pixB = lerp(n[1][1].z, clamp(n[1][1].z, minB, maxB), sharpenB);
}
#endif