// single source of truth for the MIX24 panel layout (mm); the same grid as MIX-12 / MIX-8 with 6 strips
#pragma once
namespace MXL {
static const float PANEL_W = 193.04f;   // 38 HP
static const float CX[6] = {9.500f, 24.000f, 38.500f, 53.000f, 67.500f, 82.000f};
static const float KY[7] = {20.000f, 31.000f, 42.000f, 53.000f, 64.000f, 75.000f, 86.000f};
static const float MB_Y = 98.00f;
static const float MB_DX = 3.70f;
static const float LJ_Y = 111.40f;
static const float RJ_Y = 120.20f;
static const float JDX = 0.00f;
// the FX column sits next to the strips, MASTER at the right edge
static const float MCOL = 134.0f, MCOL_W = 56.4f;
static const float MKX[5] = {MCOL + 6.8f, MCOL + 17.4f, MCOL + 28.0f, MCOL + 38.6f, MCOL + 49.2f};
static const float MKY = 21.00f;
static const float JX[4] = {MCOL + 11.1f, MCOL + 22.5f, MCOL + 33.9f, MCOL + 45.3f};
static const float JY[2] = {104.400f, 115.600f};
static const float D1[4] = {MCOL + 1.5f, 35.600f, 53.400f, 56.000f};
// FX column, as on MIX-12 / MIX-8 (MATRIX-DUO's final stage): P1-P3 small ring knobs, big RETURN, PROGRAM,
// then the send / return jacks along the foot of the column
static const float FCOL = 93.5f, FCOL_W = 38.1f, FC = FCOL + FCOL_W * 0.5f;
static const float D2[4] = {FCOL + 1.5f, 16.200f, FCOL_W - 3.f, 30.000f};
static const float FXC[2] = {FC, 100.000f};
static const float FXJ_X[4] = {FC - 13.2f, FC - 4.4f, FC + 4.4f, FC + 13.2f};
static const float FXJ_Y = 118.20f;
static const float PX[3] = {FC - 12.f, FC, FC + 12.f};
static const float PY = 54.50f;
static const float RET[2] = {FC, 76.000f};
}
