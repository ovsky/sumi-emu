// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Based on AMD FidelityFX CAS shader code

#define AF1 float
#define AF2 vec2
#define AF3 vec3
#define AF4 vec4

#define AU1 uint
#define AU2 uvec2
#define AU3 uvec3
#define AU4 uvec4

#define ASU1 int
#define ASU2 ivec2
#define ASU3 ivec3
#define ASU4 ivec4

//------------------------------------------------------------------------------------------------------------------------------
// CAS uses a simplified path for FP16/FP32 inputs and outputs.
// To enable the simplified path, use the following.
#define CAS_SIMPLIFIED 1
#define CAS_SIMPLIFIED_INPUT AF3
#define CAS_SIMPLIFIED_OUTPUT AF3

// CAS uses a more complex path for FP32 inputs and outputs with optional scaling.
// To enable the complex path, use the following.
//#define CAS_BETTER_DIAGONALS 1
//#define CAS_GO_SLOWER 1

#include "ffx_cas.glsl"