// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <tuple>
#include <stdint.h>

#if defined(ARCHITECTURE_x86_64)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <immintrin.h>
#endif
#elif defined(ARCHITECTURE_arm64)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-int-conversion"
#include <sse2neon.h>
#pragma GCC diagnostic pop
#endif

extern "C" {
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include <libswscale/swscale.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
}

#include "common/alignment.h"
#include "common/assert.h"
#include "common/bit_field.h"
#include "common/logging/log.h"
#include "common/polyfill_thread.h"
#include "common/settings.h"

#include "video_core/engines/maxwell_3d.h"
#include "video_core/guest_memory.h"
#include "video_core/host1x/host1x.h"
#include "video_core/host1x/nvdec.h"
#include "video_core/host1x/vic.h"
#include "video_core/memory_manager.h"
#include "video_core/textures/decoders.h"

#if defined(ARCHITECTURE_x86_64)
#include "common/x64/cpu_detect.h"
#endif

namespace Tegra::Host1x {
namespace {
static bool HasSSE41() {
#if defined(ARCHITECTURE_x86_64)
    const auto& cpu_caps{Common::GetCPUCaps()};
    return cpu_caps.sse4_1;
#else
    return false;
#endif
}

void SwizzleSurface(std::span<u8> output, u32 out_stride, std::span<const u8> input, u32 in_stride,
                    u32 height) {
    /*
     * Taken from https://github.com/averne/FFmpeg/blob/nvtegra/libavutil/hwcontext_nvtegra.c#L949
     * Can only handle block height == 1.
     */
    const uint32_t x_mask = 0xFFFFFFD2u;
    const uint32_t y_mask = 0x2Cu;
    uint32_t offs_x{};
    uint32_t offs_y{};
    uint32_t offs_line{};

    for (u32 y = 0; y < height; y += 2) {
        auto dst_line = output.data() + offs_y * 16;
        const auto src_line = input.data() + y * (in_stride / 16) * 16;

        offs_line = offs_x;
        for (u32 x = 0; x < in_stride; x += 16) {
            std::memcpy(&dst_line[offs_line * 16], &src_line[x], 16);
            std::memcpy(&dst_line[offs_line * 16 + 16], &src_line[x + in_stride], 16);
            offs_line = (offs_line - x_mask) & x_mask;
        }

        offs_y = (offs_y - y_mask) & y_mask;

        /* Wrap into next tile row */
        if (!offs_y) {
            offs_x += out_stride;
        }
    }
}

} // namespace

Vic::Vic(Host1x& host1x_, s32 id_, u32 syncpt, FrameQueue& frame_queue_)
    : CDmaPusher{host1x_, id_}, id{id_}, syncpoint{syncpt},
      frame_queue{frame_queue_}, has_sse41{HasSSE41()} {
    LOG_INFO(HW_GPU, "Created vic {}", id);
}

Vic::~Vic() {
    LOG_INFO(HW_GPU, "Destroying vic {}", id);
    frame_queue.Close(id);
}

void Vic::ProcessMethod(u32 method, u32 arg) {
    LOG_TRACE(HW_GPU, "Vic {} method 0x{:X}", id, static_cast<u32>(method));
    regs.reg_array[method] = arg;

    switch (static_cast<Method>(method * sizeof(u32))) {
    case Method::Execute: {
        Execute();
    } break;
    default:
        break;
    }
}

void Vic::Execute() {
    ConfigStruct config{};
    memory_manager.ReadBlock(regs.config_struct_offset.Address(), &config, sizeof(ConfigStruct));

    auto output_width{config.output_surface_config.out_surface_width + 1};
    auto output_height{config.output_surface_config.out_surface_height + 1};
    output_surface.resize_destructive(output_width * output_height);

    if (Settings::values.nvdec_emulation.GetValue() == Settings::NvdecEmulation::Off) [[unlikely]] {
        // Fill the frame with black, as otherwise they can have random data and be very glitchy.
        std::fill(output_surface.begin(), output_surface.end(), Pixel{});
    } else {
        for (size_t i = 0; i < config.slot_structs.size(); i++) {
            auto& slot_config{config.slot_structs[i]};
            if (!slot_config.config.slot_enable) {
                continue;
            }

            auto luma_offset{regs.surfaces[i][SurfaceIndex::Current].luma.Address()};
            if (nvdec_id == -1) {
                nvdec_id = frame_queue.VicFindNvdecFdFromOffset(luma_offset);
            }

            auto frame = frame_queue.GetFrame(nvdec_id, luma_offset);
            if (!frame.get()) {
                LOG_ERROR(HW_GPU, "Vic {} failed to get frame with offset 0x{:X}", id, luma_offset);
                continue;
            }

            switch (frame->GetPixelFormat()) {
            case AV_PIX_FMT_YUV420P:
                ReadY8__V8U8_N420<true>(slot_config, regs.surfaces[i], std::move(frame));
                break;
            case AV_PIX_FMT_NV12:
                ReadY8__V8U8_N420<false>(slot_config, regs.surfaces[i], std::move(frame));
                break;
            default:
                UNIMPLEMENTED_MSG(
                    "Unimplemented slot pixel format {}",
                    static_cast<u32>(slot_config.surface_config.slot_pixel_format.Value()));
                break;
            }

            Blend(config, slot_config);
        }
    }

    switch (config.output_surface_config.out_pixel_format) {
    case VideoPixelFormat::A8B8G8R8:
    case VideoPixelFormat::X8B8G8R8:
        WriteABGR<VideoPixelFormat::A8B8G8R8>(config.output_surface_config);
        break;
    case VideoPixelFormat::A8R8G8B8:
        WriteABGR<VideoPixelFormat::A8R8G8B8>(config.output_surface_config);
        break;
    case VideoPixelFormat::Y8__V8U8_N420:
        WriteY8__V8U8_N420(config.output_surface_config);
        break;
    default:
        UNIMPLEMENTED_MSG("Unknown video pixel format {}",
                          config.output_surface_config.out_pixel_format.Value());
        break;
    }
}

template <bool Planar, bool Interlaced>
void Vic::ReadProgressiveY8__V8U8_N420(const SlotStruct& slot,
                                       std::span<const PlaneOffsets> offsets,
                                       std::shared_ptr<const FFmpeg::Frame> frame) {
    const auto out_luma_width{slot.surface_config.slot_surface_width + 1};
    auto out_luma_height{slot.surface_config.slot_surface_height + 1};
    const auto out_luma_stride{out_luma_width};

    if constexpr (Interlaced) {
        out_luma_height *= 2;
    }

    slot_surface.resize_destructive(out_luma_width * out_luma_height);

    const auto in_luma_width{std::min(frame->GetWidth(), static_cast<s32>(out_luma_width))};
    const auto in_luma_height{std::min(frame->GetHeight(), static_cast<s32>(out_luma_height))};
    const auto in_luma_stride{frame->GetStride(0)};

    const auto in_chroma_stride{frame->GetStride(1)};

    const auto* luma_buffer{frame->GetPlane(0)};
    const auto* chroma_u_buffer{frame->GetPlane(1)};
    const auto* chroma_v_buffer{frame->GetPlane(2)};

    LOG_TRACE(HW_GPU,
              "Reading frame"
              "\ninput luma {}x{} stride {} chroma {}x{} stride {}\n"
              "output luma {}x{} stride {} chroma {}x{} stride {}",
              in_luma_width, in_luma_height, in_luma_stride, in_luma_width / 2, in_luma_height / 2,
              in_chroma_stride, out_luma_width, out_luma_height, out_luma_stride, out_luma_width,
              out_luma_height, out_luma_stride);

    [[maybe_unused]] auto DecodeLinear = [&]() {
        const auto alpha{static_cast<u16>(slot.config.planar_alpha.Value())};

        for (s32 y = 0; y < in_luma_height; y++) {
            const auto src_luma{y * in_luma_stride};
            const auto src_chroma{(y / 2) * in_chroma_stride};
            const auto dst{y * out_luma_stride};
            for (s32 x = 0; x < in_luma_width; x++) {
                slot_surface[dst + x].r = static_cast<u16>(luma_buffer[src_luma + x] << 2);
                // Chroma samples are duplicated horizontally and vertically.
                if constexpr (Planar) {
                    slot_surface[dst + x].g =
                        static_cast<u16>(chroma_u_buffer[src_chroma + x / 2] << 2);
                    slot_surface[dst + x].b =
                        static_cast<u16>(chroma_v_buffer[src_chroma + x / 2] << 2);
                } else {
                    slot_surface[dst + x].g =
                        static_cast<u16>(chroma_u_buffer[src_chroma + (x & ~1) + 0] << 2);
                    slot_surface[dst + x].b =
                        static_cast<u16>(chroma_u_buffer[src_chroma + (x & ~1) + 1] << 2);
                }
                slot_surface[dst + x].a = alpha;
            }
        }
    };

#if defined(ARCHITECTURE_x86_64)
    if (!has_sse41) {
        DecodeLinear();
        return;
    }
#endif

#if defined(ARCHITECTURE_x86_64) || defined(ARCHITECTURE_arm64)
    const auto alpha_linear{static_cast<u16>(slot.config.planar_alpha.Value())};
    const auto alpha =
        _mm_slli_epi64(_mm_set1_epi64x(static_cast<s64>(slot.config.planar_alpha.Value())), 48);

    const auto shuffle_mask = _mm_set_epi8(13, 15, 14, 12, 9, 11, 10, 8, 5, 7, 6, 4, 1, 3, 2, 0);
    const auto sse_aligned_width = Common::AlignDown(in_luma_width, 16);

    for (s32 y = 0; y < in_luma_height; y++) {
        const auto src_luma{y * in_luma_stride};
        const auto src_chroma{(y / 2) * in_chroma_stride};
        const auto dst{y * out_luma_stride};
        s32 x = 0;
        for (; x < sse_aligned_width; x += 16) {
            // clang-format off
            // Prefetch next iteration's memory
            _mm_prefetch((const char*)&luma_buffer[src_luma + x + 16], _MM_HINT_T0);

            // Load 8 bytes * 2 of 8-bit luma samples
            // luma0 = 00 00 00 00 00 00 00 00 LL LL LL LL LL LL LL LL
            auto luma0 = _mm_loadl_epi64((__m128i*)&luma_buffer[src_luma + x + 0]);
            auto luma1 = _mm_loadl_epi64((__m128i*)&luma_buffer[src_luma + x + 8]);

            __m128i chroma;

            if constexpr (Planar) {
                _mm_prefetch((const char*)&chroma_u_buffer[src_chroma + x / 2 + 8], _MM_HINT_T0);
                _mm_prefetch((const char*)&chroma_v_buffer[src_chroma + x / 2 + 8], _MM_HINT_T0);

                // If Chroma is planar, we have separate U and V planes, load 8 bytes of each
                // chroma_u0 = 00 00 00 00 00 00 00 00 UU UU UU UU UU UU UU UU
                // chroma_v0 = 00 00 00 00 00 00 00 00 VV VV VV VV VV VV VV VV
                auto chroma_u0 = _mm_loadl_epi64((__m128i*)&chroma_u_buffer[src_chroma + x / 2]);
                auto chroma_v0 = _mm_loadl_epi64((__m128i*)&chroma_v_buffer[src_chroma + x / 2]);

                // Interleave the 8 bytes of U and V into a single 16 byte reg
                // chroma = VV UU VV UU VV UU VV UU VV UU VV UU VV UU VV UU
                chroma = _mm_unpacklo_epi8(chroma_u0, chroma_v0);
            } else {
                _mm_prefetch((const char*)&chroma_u_buffer[src_chroma + x / 2 + 8], _MM_HINT_T0);

                // Chroma is already interleaved in semiplanar format, just load 16 bytes
                // chroma = VV UU VV UU VV UU VV UU VV UU VV UU VV UU VV UU
                chroma = _mm_load_si128((__m128i*)&chroma_u_buffer[src_chroma + x]);
            }

            // Convert the low 8 bytes of 8-bit luma into 16-bit luma
            // luma0 = [00] [00] [00] [00] [00] [00] [00] [00] [LL] [LL] [LL] [LL] [LL] [LL] [LL] [LL]
            // ->
            // luma0 = [00 LL] [00 LL] [00 LL] [00 LL] [00 LL] [00 LL] [00 LL] [00 LL]
            luma0 = _mm_cvtepu8_epi16(luma0);
            luma1 = _mm_cvtepu8_epi16(luma1);

            // Treat the 8 bytes of 8-bit chroma as 16-bit channels, this allows us to take both the
            // U and V together as one element. Using chroma twice here duplicates the values, as we
            // take element 0 from chroma, and then element 0 from chroma again, etc. We need to
            // duplicate chroma horitonally as chroma is half the width of luma.
            // chroma   = [VV8 UU8] [VV7 UU7] [VV6 UU6] [VV5 UU5] [VV4 UU4] [VV3 UU3] [VV2 UU2] [VV1 UU1]
            // ->
            // chroma00 = [VV4 UU4] [VV4 UU4] [VV3 UU3] [VV3 UU3] [VV2 UU2] [VV2 UU2] [VV1 UU1] [VV1 UU1]
            // chroma01 = [VV8 UU8] [VV8 UU8] [VV7 UU7] [VV7 UU7] [VV6 UU6] [VV6 UU6] [VV5 UU5] [VV5 UU5]
            auto chroma00 = _mm_unpacklo_epi16(chroma, chroma);
            auto chroma01 = _mm_unpackhi_epi16(chroma, chroma);

            // Interleave the 16-bit luma and chroma.
            // luma0    = [008 LL8] [007 LL7] [006 LL6] [005 LL5] [004 LL4] [003 LL3] [002 LL2] [001 LL1]
            // chroma00 = [VV8 UU8] [VV7 UU7] [VV6 UU6] [VV5 UU5] [VV4 UU4] [VV3 UU3] [VV2 UU2] [VV1 UU1]
            // ->
            // yuv0     = [VV4 UU4 004 LL4] [VV3 UU3 003 LL3] [VV2 UU2 002 LL2] [VV1 UU1 001 LL1]
            // yuv1     = [VV8 UU8 008 LL8] [VV7 UU7 007 LL7] [VV6 UU6 006 LL6] [VV5 UU5 005 LL5]
            auto yuv0 = _mm_unpacklo_epi16(luma0, chroma00);
            auto yuv1 = _mm_unpackhi_epi16(luma0, chroma00);
            auto yuv2 = _mm_unpacklo_epi16(luma1, chroma01);
            auto yuv3 = _mm_unpackhi_epi16(luma1, chroma01);

            // Shuffle the luma/chroma into the channel ordering we actually want. The high byte of
            // the luma which is now a constant 0 after converting 8-bit -> 16-bit is used as the
            // alpha. Luma -> R, U -> G, V -> B, 0 -> A
            // yuv0 = [VV4 UU4 004 LL4] [VV3 UU3 003 LL3] [VV2 UU2 002 LL2] [VV1 UU1 001 LL1]
            // ->
            // yuv0 = [AA4 VV4 UU4 LL4] [AA3 VV3 UU3 LL3] [AA2 VV2 UU2 LL2] [AA1 VV1 UU1 LL1]
            yuv0 = _mm_shuffle_epi8(yuv0, shuffle_mask);
            yuv1 = _mm_shuffle_epi8(yuv1, shuffle_mask);
            yuv2 = _mm_shuffle_epi8(yuv2, shuffle_mask);
            yuv3 = _mm_shuffle_epi8(yuv3, shuffle_mask);

            // Extend the 8-bit channels we have into 16-bits, as that's the target surface format.
            // Since this turns just the low 8 bytes into 16 bytes, the second of
            // each operation here right shifts the register by 8 to get the high pixels.
            // yuv0  = [AA4] [VV4] [UU4] [LL4] [AA3] [VV3] [UU3] [LL3] [AA2] [VV2] [UU2] [LL2] [AA1] [VV1] [UU1] [LL1]
            // ->
            // yuv01 = [002 AA2] [002 VV2] [002 UU2] [002 LL2] [001 AA1] [001 VV1] [001 UU1] [001 LL1]
            // yuv23 = [004 AA4] [004 VV4] [004 UU4] [004 LL4] [003 AA3] [003 VV3] ]003 UU3] [003 LL3]
            auto yuv01 = _mm_cvtepu8_epi16(yuv0);
            auto yuv23 = _mm_cvtepu8_epi16(_mm_srli_si128(yuv0, 8));
            auto yuv45 = _mm_cvtepu8_epi16(yuv1);
            auto yuv67 = _mm_cvtepu8_epi16(_mm_srli_si128(yuv1, 8));
            auto yuv89 = _mm_cvtepu8_epi16(yuv2);
            auto yuv1011 = _mm_cvtepu8_epi16(_mm_srli_si128(yuv2, 8));
            auto yuv1213 = _mm_cvtepu8_epi16(yuv3);
            auto yuv1415 = _mm_cvtepu8_epi16(_mm_srli_si128(yuv3, 8));

            // Left-shift all 16-bit channels by 2, this is to get us into a 10-bit format instead
            // of 8, which is the format alpha is in, as well as other blending values.
            yuv01 = _mm_slli_epi16(yuv01, 2);
            yuv23 = _mm_slli_epi16(yuv23, 2);
            yuv45 = _mm_slli_epi16(yuv45, 2);
            yuv67 = _mm_slli_epi16(yuv67, 2);
            yuv89 = _mm_slli_epi16(yuv89, 2);
            yuv1011 = _mm_slli_epi16(yuv1011, 2);
            yuv1213 = _mm_slli_epi16(yuv1213, 2);
            yuv1415 = _mm_slli_epi16(yuv1415, 2);

            // OR in the planar alpha, this has already been duplicated and shifted into position,
            // and just fills in the AA channels with the actual alpha value.
            yuv01 = _mm_or_si128(yuv01, alpha);
            yuv23 = _mm_or_si128(yuv23, alpha);
            yuv45 = _mm_or_si128(yuv45, alpha);
            yuv67 = _mm_or_si128(yuv67, alpha);
            yuv89 = _mm_or_si128(yuv89, alpha);
            yuv1011 = _mm_or_si128(yuv1011, alpha);
            yuv1213 = _mm_or_si128(yuv1213, alpha);
            yuv1415 = _mm_or_si128(yuv1415, alpha);

            // Store out the pixels. One pixel is now 8 bytes, so each store is 2 pixels.
            // [AA AA] [VV VV] [UU UU] [LL LL] [AA AA] [VV VV] [UU UU] [LL LL]
            _mm_store_si128((__m128i*)&slot_surface[dst + x + 0], yuv01);
            _mm_store_si128((__m128i*)&slot_surface[dst + x + 2], yuv23);
            _mm_store_si128((__m128i*)&slot_surface[dst + x + 4], yuv45);
            _mm_store_si128((__m128i*)&slot_surface[dst + x + 6], yuv67);
            _mm_store_si128((__m128i*)&slot_surface[dst + x + 8], yuv89);
            _mm_store_si128((__m128i*)&slot_surface[dst + x + 10], yuv1011);
            _mm_store_si128((__m128i*)&slot_surface[dst + x + 12], yuv1213);
            _mm_store_si128((__m128i*)&slot_surface[dst + x + 14], yuv1415);

            // clang-format on
        }

        for (; x < in_luma_width; x++) {
            slot_surface[dst + x].r = static_cast<u16>(luma_buffer[src_luma + x] << 2);
            // Chroma samples are duplicated horizontally and vertically.
            if constexpr (Planar) {
                slot_surface[dst + x].g =
                    static_cast<u16>(chroma_u_buffer[src_chroma + x / 2] << 2);
                slot_surface[dst + x].b =
                    static_cast<u16>(chroma_v_buffer[src_chroma + x / 2] << 2);
            } else {
                slot_surface[dst + x].g =
                    static_cast<u16>(chroma_u_buffer[src_chroma + (x & ~1) + 0] << 2);
                slot_surface[dst + x].b =
                    static_cast<u16>(chroma_u_buffer[src_chroma + (x & ~1) + 1] << 2);
            }
            slot_surface[dst + x].a = alpha_linear;
        }
    }
#else
    DecodeLinear();
#endif
}

template <bool Planar, bool TopField>
void Vic::ReadInterlacedY8__V8U8_N420(const SlotStruct& slot, std::span<const PlaneOffsets> offsets,
                                      std::shared_ptr<const FFmpeg::Frame> frame) {
    if constexpr (!Planar) {
        ReadProgressiveY8__V8U8_N420<Planar, true>(slot, offsets, std::move(frame));
        return;
    }
    const auto out_luma_width{slot.surface_config.slot_surface_width + 1};
    const auto out_luma_height{(slot.surface_config.slot_surface_height + 1) * 2};
    const auto out_luma_stride{out_luma_width};

    slot_surface.resize_destructive(out_luma_width * out_luma_height);

    const auto in_luma_width{std::min(frame->GetWidth(), static_cast<s32>(out_luma_width))};
    [[maybe_unused]] const auto in_luma_height{
        std::min(frame->GetHeight(), static_cast<s32>(out_luma_height))};
    const auto in_luma_stride{frame->GetStride(0)};

    [[maybe_unused]] const auto in_chroma_width{(frame->GetWidth() + 1) / 2};
    const auto in_chroma_height{(frame->GetHeight() + 1) / 2};
    const auto in_chroma_stride{frame->GetStride(1)};

    const auto* luma_buffer{frame->GetPlane(0)};
    const auto* chroma_u_buffer{frame->GetPlane(1)};
    const auto* chroma_v_buffer{frame->GetPlane(2)};

    LOG_TRACE(HW_GPU,
              "Reading frame"
              "\ninput luma {}x{} stride {} chroma {}x{} stride {}\n"
              "output luma {}x{} stride {} chroma {}x{} stride {}",
              in_luma_width, in_luma_height, in_luma_stride, in_chroma_width, in_chroma_height,
              in_chroma_stride, out_luma_width, out_luma_height, out_luma_stride,
              out_luma_width / 2, out_luma_height / 2, out_luma_stride);

    [[maybe_unused]] auto DecodeLinear = [&]() {
        auto DecodeBobField = [&]() {
            const auto alpha{static_cast<u16>(slot.config.planar_alpha.Value())};

            for (s32 y = static_cast<s32>(TopField == false); y < in_chroma_height * 2; y += 2) {
                const auto src_luma{y * in_luma_stride};
                const auto src_chroma{(y / 2) * in_chroma_stride};
                const auto dst{y * out_luma_stride};
                for (s32 x = 0; x < in_luma_width; x++) {
                    slot_surface[dst + x].r = static_cast<u16>(luma_buffer[src_luma + x] << 2);
                    if constexpr (Planar) {
                        slot_surface[dst + x].g =
                            static_cast<u16>(chroma_u_buffer[src_chroma + x / 2] << 2);
                        slot_surface[dst + x].b =
                            static_cast<u16>(chroma_v_buffer[src_chroma + x / 2] << 2);
                    } else {
                        slot_surface[dst + x].g =
                            static_cast<u16>(chroma_u_buffer[src_chroma + (x & ~1) + 0] << 2);
                        slot_surface[dst + x].b =
                            static_cast<u16>(chroma_u_buffer[src_chroma + (x & ~1) + 1] << 2);
                    }
                    slot_surface[dst + x].a = alpha;
                }

                s32 other_line{};
                if constexpr (TopField) {
                    other_line = (y + 1) * out_luma_stride;
                } else {
                    other_line = (y - 1) * out_luma_stride;
                }
                std::memcpy(&slot_surface[other_line], &slot_surface[dst],
                            out_luma_width * sizeof(Pixel));
            }
        };

        switch (slot.config.deinterlace_mode) {
        case DXVAHD_DEINTERLACE_MODE_PRIVATE::WEAVE:
            // Due to the fact that we do not write to memory in nvdec, we cannot use Weave as it
            // relies on the previous frame.
            DecodeBobField();
            break;
        case DXVAHD_DEINTERLACE_MODE_PRIVATE::BOB_FIELD:
            DecodeBobField();
            break;
        case DXVAHD_DEINTERLACE_MODE_PRIVATE::DISI1:
            // Due to the fact that we do not write to memory in nvdec, we cannot use DISI1 as it
            // relies on previous/next frames.
            DecodeBobField();
            break;
        default:
            UNIMPLEMENTED_MSG("Deinterlace mode {} not implemented!",
                              static_cast<s32>(slot.config.deinterlace_mode.Value()));
            break;
        }
    };

    DecodeLinear();
}

template <bool Planar>
void Vic::ReadY8__V8U8_N420(const SlotStruct& slot, std::span<const PlaneOffsets> offsets,
                            std::shared_ptr<const FFmpeg::Frame> frame) {
    switch (slot.config.frame_format) {
    case DXVAHD_FRAME_FORMAT::PROGRESSIVE:
        ReadProgressiveY8__V8U8_N420<Planar>(slot, offsets, std::move(frame));
        break;
    case DXVAHD_FRAME_FORMAT::TOP_FIELD:
        ReadInterlacedY8__V8U8_N420<Planar, true>(slot, offsets, std::move(frame));
        break;
    case DXVAHD_FRAME_FORMAT::BOTTOM_FIELD:
        ReadInterlacedY8__V8U8_N420<Planar, false>(slot, offsets, std::move(frame));
        break;
    default:
        LOG_ERROR(HW_GPU, "Unknown deinterlace format {}",
                  static_cast<s32>(slot.config.frame_format.Value()));
        break;
    }
}

void Vic::Blend(const ConfigStruct& config, const SlotStruct& slot) {
    constexpr auto add_one([](u32 v) -> u32 { return v != 0 ? v + 1 : 0; });

    auto source_left{add_one(static_cast<u32>(slot.config.source_rect_left.Value()))};
    auto source_right{add_one(static_cast<u32>(slot.config.source_rect_right.Value()))};
    auto source_top{add_one(static_cast<u32>(slot.config.source_rect_top.Value()))};
    auto source_bottom{add_one(static_cast<u32>(slot.config.source_rect_bottom.Value()))};

    const auto dest_left{add_one(static_cast<u32>(slot.config.dest_rect_left.Value()))};
    const auto dest_right{add_one(static_cast<u32>(slot.config.dest_rect_right.Value()))};
    const auto dest_top{add_one(static_cast<u32>(slot.config.dest_rect_top.Value()))};
    const auto dest_bottom{add_one(static_cast<u32>(slot.config.dest_rect_bottom.Value()))};

    auto rect_left{add_one(config.output_config.target_rect_left.Value())};
    auto rect_right{add_one(config.output_config.target_rect_right.Value())};
    auto rect_top{add_one(config.output_config.target_rect_top.Value())};
    auto rect_bottom{add_one(config.output_config.target_rect_bottom.Value())};

    rect_left = std::max(rect_left, dest_left);
    rect_right = std::min(rect_right, dest_right);
    rect_top = std::max(rect_top, dest_top);
    rect_bottom = std::min(rect_bottom, dest_bottom);

    source_left = std::max(source_left, rect_left);
    source_right = std::min(source_right, rect_right);
    source_top = std::max(source_top, rect_top);
    source_bottom = std::min(source_bottom, rect_bottom);

    if (source_left >= source_right || source_top >= source_bottom) {
        return;
    }

    const auto out_surface_width{config.output_surface_config.out_surface_width + 1};
    [[maybe_unused]] const auto out_surface_height{config.output_surface_config.out_surface_height +
                                                   1};
    const auto in_surface_width{slot.surface_config.slot_surface_width + 1};

    source_bottom = std::min(source_bottom, out_surface_height);
    source_right = std::min(source_right, out_surface_width);

    // TODO Alpha blending. No games I've seen use more than a single surface or supply an alpha
    // below max, so it's ignored for now.

    if (!slot.color_matrix.matrix_enable) {
        const auto copy_width = std::min(source_right - source_left, rect_right - rect_left);

        for (u32 y = source_top; y < source_bottom; y++) {
            const auto dst_line = y * out_surface_width;
            const auto src_line = y * in_surface_width;
            std::memcpy(&output_surface[dst_line + rect_left],
                        &slot_surface[src_line + source_left], copy_width * sizeof(Pixel));
        }
    } else {
        // clang-format off
        // Colour conversion is enabled, this is a 3x4 * 4x1 matrix multiplication, resulting in a 3x1 matrix.
        // | r0c0 r0c1 r0c2 r0c3 |   | R |   | R |
        // | r1c0 r1c1 r1c2 r1c3 | * | G | = | G |
        // | r2c0 r2c1 r2c2 r2c3 |   | B |   | B |
        //                           | 1 |
        // clang-format on

        [[maybe_unused]] auto DecodeLinear = [&]() {
            const auto r0c0 = static_cast<s32>(slot.color_matrix.matrix_coeff00.Value());
            const auto r0c1 = static_cast<s32>(slot.color_matrix.matrix_coeff01.Value());
            const auto r0c2 = static_cast<s32>(slot.color_matrix.matrix_coeff02.Value());
            const auto r0c3 = static_cast<s32>(slot.color_matrix.matrix_coeff03.Value());
            const auto r1c0 = static_cast<s32>(slot.color_matrix.matrix_coeff10.Value());
            const auto r1c1 = static_cast<s32>(slot.color_matrix.matrix_coeff11.Value());
            const auto r1c2 = static_cast<s32>(slot.color_matrix.matrix_coeff12.Value());
            const auto r1c3 = static_cast<s32>(slot.color_matrix.matrix_coeff13.Value());
            const auto r2c0 = static_cast<s32>(slot.color_matrix.matrix_coeff20.Value());
            const auto r2c1 = static_cast<s32>(slot.color_matrix.matrix_coeff21.Value());
            const auto r2c2 = static_cast<s32>(slot.color_matrix.matrix_coeff22.Value());
            const auto r2c3 = static_cast<s32>(slot.color_matrix.matrix_coeff23.Value());

            const auto shift = static_cast<s32>(slot.color_matrix.matrix_r_shift.Value());
            const auto clamp_min = static_cast<s32>(slot.config.soft_clamp_low.Value());
            const auto clamp_max = static_cast<s32>(slot.config.soft_clamp_high.Value());

            auto MatMul = [&](const Pixel& in_pixel) -> std::tuple<s32, s32, s32, s32> {
                auto r = static_cast<s32>(in_pixel.r);
                auto g = static_cast<s32>(in_pixel.g);
                auto b = static_cast<s32>(in_pixel.b);

                r = in_pixel.r * r0c0 + in_pixel.g * r0c1 + in_pixel.b * r0c2;
                g = in_pixel.r * r1c0 + in_pixel.g * r1c1 + in_pixel.b * r1c2;
                b = in_pixel.r * r2c0 + in_pixel.g * r2c1 + in_pixel.b * r2c2;

                r >>= shift;
                g >>= shift;
                b >>= shift;

                r += r0c3;
                g += r1c3;
                b += r2c3;

                r >>= 8;
                g >>= 8;
                b >>= 8;

                return {r, g, b, static_cast<s32>(in_pixel.a)};
            };

            for (u32 y = source_top; y < source_bottom; y++) {
                const auto src{y * in_surface_width + source_left};
                const auto dst{y * out_surface_width + rect_left};
                for (u32 x = source_left; x < source_right; x++) {
                    auto [r, g, b, a] = MatMul(slot_surface[src + x]);

                    r = std::clamp(r, clamp_min, clamp_max);
                    g = std::clamp(g, clamp_min, clamp_max);
                    b = std::clamp(b, clamp_min, clamp_max);
                    a = std::clamp(a, clamp_min, clamp_max);

                    output_surface[dst + x] = {static_cast<u16>(r), static_cast<u16>(g),
                                               static_cast<u16>(b), static_cast<u16>(a)};
                }
            }
        };

#if defined(ARCHITECTURE_x86_64)
        if (!has_sse41) {
            DecodeLinear();
            return;
        }
#endif

#if defined(ARCHITECTURE_x86_64) || defined(ARCHITECTURE_arm64)
        // Fill the columns, e.g
        // c0 = [00 00 00 00] [r2c0 r2c0 r2c0 r2c0] [r1c0 r1c0 r1c0 r1c0] [r0c0 r0c0 r0c0 r0c0]

        const auto c0 = _mm_set_epi32(0, static_cast<s32>(slot.color_matrix.matrix_coeff20.Value()),
                                      static_cast<s32>(slot.color_matrix.matrix_coeff10.Value()),
                                      static_cast<s32>(slot.color_matrix.matrix_coeff00.Value()));
        const auto c1 = _mm_set_epi32(0, static_cast<s32>(slot.color_matrix.matrix_coeff21.Value()),
                                      static_cast<s32>(slot.color_matrix.matrix_coeff11.Value()),
                                      static_cast<s32>(slot.color_matrix.matrix_coeff01.Value()));
        const auto c2 = _mm_set_epi32(0, static_cast<s32>(slot.color_matrix.matrix_coeff22.Value()),
                                      static_cast<s32>(slot.color_matrix.matrix_coeff12.Value()),
                                      static_cast<s32>(slot.color_matrix.matrix_coeff02.Value()));
        const auto c3 = _mm_set_epi32(0, static_cast<s32>(slot.color_matrix.matrix_coeff23.Value()),
                                      static_cast<s32>(slot.color_matrix.matrix_coeff13.Value()),
                                      static_cast<s32>(slot.color_matrix.matrix_coeff03.Value()));

        // Set the matrix right-shift as a single element.
        const auto shift =
            _mm_set_epi32(0, 0, 0, static_cast<s32>(slot.color_matrix.matrix_r_shift.Value()));

        // Set every 16-bit value to the soft clamp values for clamping every 16-bit channel.
        const auto clamp_min = _mm_set1_epi16(static_cast<u16>(slot.config.soft_clamp_low.Value()));
        const auto clamp_max =
            _mm_set1_epi16(static_cast<u16>(slot.config.soft_clamp_high.Value()));

        // clang-format off

        auto MatMul = [](__m128i& p, const __m128i& col0, const __m128i& col1, const __m128i& col2,
                         const __m128i& col3, const __m128i& trm_shift) -> __m128i {
            // Duplicate the 32-bit channels, e.g
            // p = [AA AA AA AA] [BB BB BB BB] [GG GG GG GG] [RR RR RR RR]
            // ->
            // r = [RR4 RR4 RR4 RR4] [RR3 RR3 RR3 RR3] [RR2 RR2 RR2 RR2] [RR1 RR1 RR1 RR1]
            auto r = _mm_shuffle_epi32(p, 0x0);
            auto g = _mm_shuffle_epi32(p, 0x55);
            auto b = _mm_shuffle_epi32(p, 0xAA);

            // Multiply the rows and columns c0 * r, c1 * g, c2 * b, e.g
            // r  = [RR4 RR4 RR4 RR4] [ RR3  RR3  RR3  RR3] [ RR2  RR2  RR2  RR2] [ RR1  RR1  RR1  RR1]
            //                                             *
            // c0 = [ 00  00  00  00] [r2c0 r2c0 r2c0 r2c0] [r1c0 r1c0 r1c0 r1c0] [r0c0 r0c0 r0c0 r0c0]
            r = _mm_mullo_epi32(r, col0);
            g = _mm_mullo_epi32(g, col1);
            b = _mm_mullo_epi32(b, col2);

            // Add them all together vertically, such that the 32-bit element
            // out[0] = (r[0] * c0[0]) + (g[0] * c1[0]) + (b[0] * c2[0])
            auto out = _mm_add_epi32(_mm_add_epi32(r, g), b);

            // Shift the result by r_shift, as the TRM says
            out = _mm_sra_epi32(out, trm_shift);

            // Add the final column. Because the 4x1 matrix has this row as 1, there's no need to
            // multiply by it, and as per the TRM this column ignores r_shift, so it's just added
            // here after shifting.
            out = _mm_add_epi32(out, col3);

            // Shift the result back from S12.8 to integer values
            return _mm_srai_epi32(out, 8);
        };

        for (u32 y = source_top; y < source_bottom; y++) {
            const auto src{y * in_surface_width + source_left};
            const auto dst{y * out_surface_width + rect_left};
            for (u32 x = source_left; x < source_right; x += 8) {
                // clang-format off
                // Prefetch the next iteration's memory
                _mm_prefetch((const char*)&slot_surface[src + x + 8], _MM_HINT_T0);

                // Load in pixels
                // p01 = [AA AA] [BB BB] [GG GG] [RR RR] [AA AA] [BB BB] [GG GG] [RR RR]
                auto p01 = _mm_load_si128((__m128i*)&slot_surface[src + x + 0]);
                auto p23 = _mm_load_si128((__m128i*)&slot_surface[src + x + 2]);
                auto p45 = _mm_load_si128((__m128i*)&slot_surface[src + x + 4]);
                auto p67 = _mm_load_si128((__m128i*)&slot_surface[src + x + 6]);

                // Convert the 16-bit channels into 32-bit (unsigned), as the matrix values are
                // 32-bit and to avoid overflow.
                // p01    = [AA2 AA2] [BB2 BB2] [GG2 GG2] [RR2 RR2] [AA1 AA1] [BB1 BB1] [GG1 GG1] [RR1 RR1]
                // ->
                // p01_lo = [001 001 AA1 AA1] [001 001 BB1 BB1] [001 001 GG1 GG1] [001 001 RR1 RR1]
                // p01_hi = [002 002 AA2 AA2] [002 002 BB2 BB2] [002 002 GG2 GG2] [002 002 RR2 RR2]
                auto p01_lo = _mm_cvtepu16_epi32(p01);
                auto p01_hi = _mm_cvtepu16_epi32(_mm_srli_si128(p01, 8));
                auto p23_lo = _mm_cvtepu16_epi32(p23);
                auto p23_hi = _mm_cvtepu16_epi32(_mm_srli_si128(p23, 8));
                auto p45_lo = _mm_cvtepu16_epi32(p45);
                auto p45_hi = _mm_cvtepu16_epi32(_mm_srli_si128(p45, 8));
                auto p67_lo = _mm_cvtepu16_epi32(p67);
                auto p67_hi = _mm_cvtepu16_epi32(_mm_srli_si128(p67, 8));

                // Matrix multiply the pixel, doing the colour conversion.
                auto out0 = MatMul(p01_lo, c0, c1, c2, c3, shift);
                auto out1 = MatMul(p01_hi, c0, c1, c2, c3, shift);
                auto out2 = MatMul(p23_lo, c0, c1, c2, c3, shift);
                auto out3 = MatMul(p23_hi, c0, c1, c2, c3, shift);
                auto out4 = MatMul(p45_lo, c0, c1, c2, c3, shift);
                auto out5 = MatMul(p45_hi, c0, c1, c2, c3, shift);
                auto out6 = MatMul(p67_lo, c0, c1, c2, c3, shift);
                auto out7 = MatMul(p67_hi, c0, c1, c2, c3, shift);

                // Pack the 32-bit channel pixels back into 16-bit using unsigned saturation
                // out0  = [001 001 AA1 AA1] [001 001 BB1 BB1] [001 001 GG1 GG1] [001 001 RR1 RR1]
                // out1  = [002 002 AA2 AA2] [002 002 BB2 BB2] [002 002 GG2 GG2] [002 002 RR2 RR2]
                // ->
                // done0 = [AA2 AA2] [BB2 BB2] [GG2 GG2] [RR2 RR2] [AA1 AA1] [BB1 BB1] [GG1 GG1] [RR1 RR1]
                auto done0 = _mm_packus_epi32(out0, out1);
                auto done1 = _mm_packus_epi32(out2, out3);
                auto done2 = _mm_packus_epi32(out4, out5);
                auto done3 = _mm_packus_epi32(out6, out7);

                // Blend the original alpha back into the pixel, as the matrix multiply gives us a
                // 3-channel output, not 4.
                // 0x88 = b10001000, taking RGB from the first argument, A from the second argument.
                // done0 = [002 002] [BB2 BB2] [GG2 GG2] [RR2 RR2] [001 001] [BB1 BB1] [GG1 GG1] [RR1 RR1]
                // ->
                // done0 = [AA2 AA2] [BB2 BB2] [GG2 GG2] [RR2 RR2] [AA1 AA1] [BB1 BB1] [GG1 GG1] [RR1 RR1]
                done0 = _mm_blend_epi16(done0, p01, 0x88);
                done1 = _mm_blend_epi16(done1, p23, 0x88);
                done2 = _mm_blend_epi16(done2, p45, 0x88);
                done3 = _mm_blend_epi16(done3, p67, 0x88);

                // Clamp the 16-bit channels to the soft-clamp min/max.
                done0 = _mm_max_epu16(done0, clamp_min);
                done1 = _mm_max_epu16(done1, clamp_min);
                done2 = _mm_max_epu16(done2, clamp_min);
                done3 = _mm_max_epu16(done3, clamp_min);

                done0 = _mm_min_epu16(done0, clamp_max);
                done1 = _mm_min_epu16(done1, clamp_max);
                done2 = _mm_min_epu16(done2, clamp_max);
                done3 = _mm_min_epu16(done3, clamp_max);

                // Store the pixels to the output surface.
                _mm_store_si128((__m128i*)&output_surface[dst + x + 0], done0);
                _mm_store_si128((__m128i*)&output_surface[dst + x + 2], done1);
                _mm_store_si128((__m128i*)&output_surface[dst + x + 4], done2);
                _mm_store_si128((__m128i*)&output_surface[dst + x + 6], done3);

            }
        }
        // clang-format on
#else
        DecodeLinear();
#endif
    }
}

void Vic::WriteY8__V8U8_N420(const OutputSurfaceConfig& output_surface_config) {
    constexpr u32 BytesPerPixel = 1;

    auto surface_width{output_surface_config.out_surface_width + 1};
    auto surface_height{output_surface_config.out_surface_height + 1};
    const auto surface_stride{surface_width};

    const auto out_luma_width = output_surface_config.out_luma_width + 1;
    const auto out_luma_height = output_surface_config.out_luma_height + 1;
    const auto out_luma_stride = Common::AlignUp(out_luma_width * BytesPerPixel, 0x10);
    const auto out_luma_size = out_luma_height * out_luma_stride;

    const auto out_chroma_width = output_surface_config.out_chroma_width + 1;
    const auto out_chroma_height = output_surface_config.out_chroma_height + 1;
    const auto out_chroma_stride = Common::AlignUp(out_chroma_width * BytesPerPixel * 2, 0x10);
    const auto out_chroma_size = out_chroma_height * out_chroma_stride;

    surface_width = std::min(surface_width, out_luma_width);
    surface_height = std::min(surface_height, out_luma_height);

    [[maybe_unused]] auto DecodeLinear = [&](std::span<u8> out_luma, std::span<u8> out_chroma) {
        for (u32 y = 0; y < surface_height; ++y) {
            const auto src_luma = y * surface_stride;
            const auto dst_luma = y * out_luma_stride;
            const auto src_chroma = y * surface_stride;
            const auto dst_chroma = (y / 2) * out_chroma_stride;
            for (u32 x = 0; x < surface_width; x += 2) {
                out_luma[dst_luma + x + 0] =
                    static_cast<u8>(output_surface[src_luma + x + 0].r >> 2);
                out_luma[dst_luma + x + 1] =
                    static_cast<u8>(output_surface[src_luma + x + 1].r >> 2);
                out_chroma[dst_chroma + x + 0] =
                    static_cast<u8>(output_surface[src_chroma + x].g >> 2);
                out_chroma[dst_chroma + x + 1] =
                    static_cast<u8>(output_surface[src_chroma + x].b >> 2);
            }
        }
    };

    auto Decode = [&](std::span<u8> out_luma, std::span<u8> out_chroma) {
#if defined(ARCHITECTURE_x86_64)
        if (!has_sse41) {
            DecodeLinear(out_luma, out_chroma);
            return;
        }
#endif

#if defined(ARCHITECTURE_x86_64) || defined(ARCHITECTURE_arm64)
        // luma_mask   = [00 00] [00 00] [00 00] [FF FF] [00 00] [00 00] [00 00] [FF FF]
        const auto luma_mask = _mm_set_epi16(0, 0, 0, -1, 0, 0, 0, -1);

        const auto sse_aligned_width = Common::AlignDown(surface_width, 16);

        for (u32 y = 0; y < surface_height; ++y) {
            const auto src = y * surface_stride;
            const auto dst_luma = y * out_luma_stride;
            const auto dst_chroma = (y / 2) * out_chroma_stride;
            u32 x = 0;
            for (; x < sse_aligned_width; x += 16) {
                // clang-format off
                // Prefetch the next cache lines, 2 per iteration
                _mm_prefetch((const char*)&output_surface[src + x + 16], _MM_HINT_T0);
                _mm_prefetch((const char*)&output_surface[src + x + 24], _MM_HINT_T0);

                // Load the 64-bit pixels, 2 per variable.
                auto pixel01 = _mm_load_si128((__m128i*)&output_surface[src + x + 0]);
                auto pixel23 = _mm_load_si128((__m128i*)&output_surface[src + x + 2]);
                auto pixel45 = _mm_load_si128((__m128i*)&output_surface[src + x + 4]);
                auto pixel67 = _mm_load_si128((__m128i*)&output_surface[src + x + 6]);
                auto pixel89 = _mm_load_si128((__m128i*)&output_surface[src + x + 8]);
                auto pixel1011 = _mm_load_si128((__m128i*)&output_surface[src + x + 10]);
                auto pixel1213 = _mm_load_si128((__m128i*)&output_surface[src + x + 12]);
                auto pixel1415 = _mm_load_si128((__m128i*)&output_surface[src + x + 14]);

                // Split out the luma of each pixel using the luma_mask above.
                // pixel01 = [AA2 AA2] [VV2 VV2] [UU2 UU2] [LL2 LL2] [AA1 AA1] [VV1 VV1] [UU1 UU1] [LL1 LL1]
                // ->
                //     l01 = [002 002] [002 002] [002 002] [LL2 LL2] [001 001] [001 001] [001 001] [LL1 LL1]
                auto l01 = _mm_and_si128(pixel01, luma_mask);
                auto l23 = _mm_and_si128(pixel23, luma_mask);
                auto l45 = _mm_and_si128(pixel45, luma_mask);
                auto l67 = _mm_and_si128(pixel67, luma_mask);
                auto l89 = _mm_and_si128(pixel89, luma_mask);
                auto l1011 = _mm_and_si128(pixel1011, luma_mask);
                auto l1213 = _mm_and_si128(pixel1213, luma_mask);
                auto l1415 = _mm_and_si128(pixel1415, luma_mask);

                // Pack 32-bit elements from 2 registers down into 16-bit elements in 1 register.
                // l01   = [002 002 002 002] [002 002 LL2 LL2] [001 001 001 001] [001 001 LL1 LL1]
                // l23   = [004 004 004 004] [004 004 LL4 LL4] [003 003 003 003] [003 003 LL3 LL3]
                // ->
                // l0123 = [004 004] [LL4 LL4] [003 003] [LL3 LL3] [002 002] [LL2 LL2] [001 001] [LL1 LL1]
                auto l0123 = _mm_packus_epi32(l01, l23);
                auto l4567 = _mm_packus_epi32(l45, l67);
                auto l891011 = _mm_packus_epi32(l89, l1011);
                auto l12131415 = _mm_packus_epi32(l1213, l1415);

                // Pack 32-bit elements from 2 registers down into 16-bit elements in 1 register.
                // l0123   = [004 004 LL4 LL4] [003 003 LL3 LL3] [002 002 LL2 LL2] [001 001 LL1 LL1]
                // l4567   = [008 008 LL8 LL8] [007 007 LL7 LL7] [006 006 LL6 LL6] [005 005 LL5 LL5]
                // ->
                // luma_lo = [LL8 LL8] [LL7 LL7] [LL6 LL6] [LL5 LL5] [LL4 LL4] [LL3 LL3] [LL2 LL2] [LL1 LL1]
                auto luma_lo = _mm_packus_epi32(l0123, l4567);
                auto luma_hi = _mm_packus_epi32(l891011, l12131415);

                // Right-shift the 16-bit elements by 2, un-doing the left shift by 2 on read
                // and bringing the range back to 8-bit.
                luma_lo = _mm_srli_epi16(luma_lo, 2);
                luma_hi = _mm_srli_epi16(luma_hi, 2);

                // Pack with unsigned saturation the 16-bit values in 2 registers into 8-bit values in 1 register.
                // luma_lo =  [LL8  LL8]  [LL7  LL7]  [LL6  LL6]  [LL5  LL5]  [LL4  LL4]  [LL3  LL3]  [LL2  LL2] [LL1 LL1]
                // luma_hi = [LL16 LL16] [LL15 LL15] [LL14 LL14] [LL13 LL13] [LL12 LL12] [LL11 LL11] [LL10 LL10] [LL9 LL9]
                // ->
                // luma = [LL16] [LL15] [LL14] [LL13] [LL12] [LL11] [LL10] [LL9] [LL8] [LL7] [LL6] [LL5] [LL4] [LL3] [LL2] [LL1]
                auto luma = _mm_packus_epi16(luma_lo, luma_hi);

                // Store the 16 bytes of luma
                _mm_store_si128((__m128i*)&out_luma[dst_luma + x], luma);

                if (y % 2 == 0) {
                    // Chroma, done every other line as it's half the height of luma.

                    // Shift the register right by 2 bytes (not bits), to kick out the 16-bit luma.
                    // We can do this instead of &'ing a mask and then shifting.
                    // pixel01 = [AA2 AA2] [VV2 VV2] [UU2 UU2] [LL2 LL2] [AA1 AA1] [VV1 VV1] [UU1 UU1] [LL1 LL1]
                    // ->
                    //     c01 = [ 00  00] [AA2 AA2] [VV2 VV2] [UU2 UU2] [LL2 LL2] [AA1 AA1] [VV1 VV1] [UU1 UU1]
                    auto c01 = _mm_srli_si128(pixel01, 2);
                    auto c23 = _mm_srli_si128(pixel23, 2);
                    auto c45 = _mm_srli_si128(pixel45, 2);
                    auto c67 = _mm_srli_si128(pixel67, 2);
                    auto c89 = _mm_srli_si128(pixel89, 2);
                    auto c1011 = _mm_srli_si128(pixel1011, 2);
                    auto c1213 = _mm_srli_si128(pixel1213, 2);
                    auto c1415 = _mm_srli_si128(pixel1415, 2);

                    // Interleave the lower 8 bytes as 32-bit elements from 2 registers into 1 register.
                    // This has the effect of skipping every other chroma value horitonally,
                    // notice the high pixels UU2/UU4 are skipped.
                    // This is intended as N420 chroma width is half the luma width.
                    // c01   = [ 00  00 AA2 AA2] [VV2 VV2 UU2 UU2] [LL2 LL2 AA1 AA1] [VV1 VV1 UU1 UU1]
                    // c23   = [ 00  00 AA4 AA4] [VV4 VV4 UU4 UU4] [LL4 LL4 AA3 AA3] [VV3 VV3 UU3 UU3]
                    // ->
                    // c0123 = [LL4 LL4 AA3 AA3] [LL2 LL2 AA1 AA1] [VV3 VV3 UU3 UU3] [VV1 VV1 UU1 UU1]
                    auto c0123 = _mm_unpacklo_epi32(c01, c23);
                    auto c4567 = _mm_unpacklo_epi32(c45, c67);
                    auto c891011 = _mm_unpacklo_epi32(c89, c1011);
                    auto c12131415 = _mm_unpacklo_epi32(c1213, c1415);

                    // Interleave the low 64-bit elements from 2 registers into 1.
                    // c0123     = [LL4 LL4 AA3 AA3 LL2 LL2 AA1 AA1] [VV3 VV3 UU3 UU3 VV1 VV1 UU1 UU1]
                    // c4567     = [LL8 LL8 AA7 AA7 LL6 LL6 AA5 AA5] [VV7 VV7 UU7 UU7 VV5 VV5 UU5 UU5]
                    // ->
                    // chroma_lo = [VV7 VV7 UU7 UU7 VV5 VV5 UU5 UU5] [VV3 VV3 UU3 UU3 VV1 VV1 UU1 UU1]
                    auto chroma_lo = _mm_unpacklo_epi64(c0123, c4567);
                    auto chroma_hi = _mm_unpacklo_epi64(c891011, c12131415);

                    // Right-shift the 16-bit elements by 2, un-doing the left shift by 2 on read
                    // and bringing the range back to 8-bit.
                    chroma_lo = _mm_srli_epi16(chroma_lo, 2);
                    chroma_hi = _mm_srli_epi16(chroma_hi, 2);

                    // Pack with unsigned saturation the 16-bit elements from 2 registers into 8-bit elements in 1 register.
                    // chroma_lo = [ VV7  VV7] [ UU7  UU7] [ VV5  VV5] [ UU5  UU5] [ VV3  VV3] [ UU3  UU3] [VV1 VV1] [UU1 UU1]
                    // chroma_hi = [VV15 VV15] [UU15 UU15] [VV13 VV13] [UU13 UU13] [VV11 VV11] [UU11 UU11] [VV9 VV9] [UU9 UU9]
                    // ->
                    // chroma    = [VV15] [UU15] [VV13] [UU13] [VV11] [UU11] [VV9] [UU9] [VV7] [UU7] [VV5] [UU5] [VV3] [UU3] [VV1] [UU1]
                    auto chroma = _mm_packus_epi16(chroma_lo, chroma_hi);

                    // Store the 16 bytes of chroma.
                    _mm_store_si128((__m128i*)&out_chroma[dst_chroma + x + 0], chroma);
                }

                // clang-format on
            }

            const auto src_chroma = y * surface_stride;
            for (; x < surface_width; x += 2) {
                out_luma[dst_luma + x + 0] = static_cast<u8>(output_surface[src + x + 0].r >> 2);
                out_luma[dst_luma + x + 1] = static_cast<u8>(output_surface[src + x + 1].r >> 2);
                out_chroma[dst_chroma + x + 0] =
                    static_cast<u8>(output_surface[src_chroma + x].g >> 2);
                out_chroma[dst_chroma + x + 1] =
                    static_cast<u8>(output_surface[src_chroma + x].b >> 2);
            }
        }
#else
        DecodeLinear(out_luma, out_chroma);
#endif
    };

    switch (output_surface_config.out_block_kind) {
    case BLK_KIND::GENERIC_16Bx2: {
        const u32 block_height = static_cast<u32>(output_surface_config.out_block_height);
        const auto out_luma_swizzle_size = Texture::CalculateSize(
            true, BytesPerPixel, out_luma_width, out_luma_height, 1, block_height, 0);
        const auto out_chroma_swizzle_size = Texture::CalculateSize(
            true, BytesPerPixel * 2, out_chroma_width, out_chroma_height, 1, block_height, 0);

        LOG_TRACE(
            HW_GPU,
            "Writing Y8__V8U8_N420 swizzled frame\n"
            "\tinput surface {}x{} stride {} size 0x{:X}\n"
            "\toutput   luma {}x{} stride {} size 0x{:X} block height {} swizzled size 0x{:X}\n",
            "\toutput chroma {}x{} stride {} size 0x{:X} block height {} swizzled size 0x{:X}",
            surface_width, surface_height, surface_stride * BytesPerPixel,
            surface_stride * surface_height * BytesPerPixel, out_luma_width, out_luma_height,
            out_luma_stride, out_luma_size, block_height, out_luma_swizzle_size, out_chroma_width,
            out_chroma_height, out_chroma_stride, out_chroma_size, block_height,
            out_chroma_swizzle_size);

        luma_scratch.resize_destructive(out_luma_size);
        chroma_scratch.resize_destructive(out_chroma_size);

        Decode(luma_scratch, chroma_scratch);

        Tegra::Memory::GpuGuestMemoryScoped<u8, Core::Memory::GuestMemoryFlags::SafeWrite> out_luma(
            memory_manager, regs.output_surface.luma.Address(), out_luma_swizzle_size,
            &swizzle_scratch);

        if (block_height == 1) {
            SwizzleSurface(out_luma, out_luma_stride, luma_scratch, out_luma_stride,
                           out_luma_height);
        } else {
            Texture::SwizzleTexture(out_luma, luma_scratch, BytesPerPixel, out_luma_width,
                                    out_luma_height, 1, block_height, 0, 1);
        }

        Tegra::Memory::GpuGuestMemoryScoped<u8, Core::Memory::GuestMemoryFlags::SafeWrite>
            out_chroma(memory_manager, regs.output_surface.chroma_u.Address(),
                       out_chroma_swizzle_size, &swizzle_scratch);

        if (block_height == 1) {
            SwizzleSurface(out_chroma, out_chroma_stride, chroma_scratch, out_chroma_stride,
                           out_chroma_height);
        } else {
            Texture::SwizzleTexture(out_chroma, chroma_scratch, BytesPerPixel, out_chroma_width,
                                    out_chroma_height, 1, block_height, 0, 1);
        }
    } break;
    case BLK_KIND::PITCH: {
        LOG_TRACE(
            HW_GPU,
            "Writing Y8__V8U8_N420 swizzled frame\n"
            "\tinput surface {}x{} stride {} size 0x{:X}\n"
            "\toutput   luma {}x{} stride {} size 0x{:X} block height {} swizzled size 0x{:X}\n",
            "\toutput chroma {}x{} stride {} size 0x{:X} block height {} swizzled size 0x{:X}",
            surface_width, surface_height, surface_stride * BytesPerPixel,
            surface_stride * surface_height * BytesPerPixel, out_luma_width, out_luma_height,
            out_luma_stride, out_luma_size, out_chroma_width, out_chroma_height, out_chroma_stride,
            out_chroma_size);

        // Unfortunately due to a driver bug or game bug, the chroma address can be not
        // appropriately spaced from the luma, so the luma of size out_stride * height runs into the
        // top of the chroma buffer. Unfortunately that removes an optimisation here where we could
        // create guest spans and decode into game memory directly to avoid the memory copy from
        // scratch to game. Due to this bug, we must write the luma first, and then the chroma
        // afterwards to re-overwrite the luma being too large.
        luma_scratch.resize_destructive(out_luma_size);
        chroma_scratch.resize_destructive(out_chroma_size);

        Decode(luma_scratch, chroma_scratch);

        memory_manager.WriteBlock(regs.output_surface.luma.Address(), luma_scratch.data(),
                                  out_luma_size);
        memory_manager.WriteBlock(regs.output_surface.chroma_u.Address(), chroma_scratch.data(),
                                  out_chroma_size);
    } break;
    default:
        UNREACHABLE();
        break;
    }
}

template <VideoPixelFormat Format>
void Vic::WriteABGR(const OutputSurfaceConfig& output_surface_config) {
    constexpr u32 BytesPerPixel = 4;

    auto surface_width{output_surface_config.out_surface_width + 1};
    auto surface_height{output_surface_config.out_surface_height + 1};
    const auto surface_stride{surface_width};

    const auto out_luma_width = output_surface_config.out_luma_width + 1;
    const auto out_luma_height = output_surface_config.out_luma_height + 1;
    const auto out_luma_stride = Common ::AlignUp(out_luma_width * BytesPerPixel, 0x10);
    const auto out_luma_size = out_luma_height * out_luma_stride;

    surface_width = std::min(surface_width, out_luma_width);
    surface_height = std::min(surface_height, out_luma_height);

    [[maybe_unused]] auto DecodeLinear = [&](std::span<u8> out_buffer) {
        for (u32 y = 0; y < surface_height; y++) {
            const auto src = y * surface_stride;
            const auto dst = y * out_luma_stride;
            for (u32 x = 0; x < surface_width; x++) {
                if constexpr (Format == VideoPixelFormat::A8R8G8B8) {
                    out_buffer[dst + x * 4 + 0] = static_cast<u8>(output_surface[src + x].b >> 2);
                    out_buffer[dst + x * 4 + 1] = static_cast<u8>(output_surface[src + x].g >> 2);
                    out_buffer[dst + x * 4 + 2] = static_cast<u8>(output_surface[src + x].r >> 2);
                    out_buffer[dst + x * 4 + 3] = static_cast<u8>(output_surface[src + x].a >> 2);
                } else {
                    out_buffer[dst + x * 4 + 0] = static_cast<u8>(output_surface[src + x].r >> 2);
                    out_buffer[dst + x * 4 + 1] = static_cast<u8>(output_surface[src + x].g >> 2);
                    out_buffer[dst + x * 4 + 2] = static_cast<u8>(output_surface[src + x].b >> 2);
                    out_buffer[dst + x * 4 + 3] = static_cast<u8>(output_surface[src + x].a >> 2);
                }
            }
        }
    };

    auto Decode = [&](std::span<u8> out_buffer) {
#if defined(ARCHITECTURE_x86_64)
        if (!has_sse41) {
            DecodeLinear(out_buffer);
            return;
        }
#endif

#if defined(ARCHITECTURE_x86_64) || defined(ARCHITECTURE_arm64)
        constexpr size_t SseAlignment = 16;
        const auto sse_aligned_width = Common::AlignDown(surface_width, SseAlignment);

        for (u32 y = 0; y < surface_height; y++) {
            const auto src = y * surface_stride;
            const auto dst = y * out_luma_stride;
            u32 x = 0;
            for (; x < sse_aligned_width; x += SseAlignment) {
                // clang-format off
                // Prefetch the next 2 cache lines
                _mm_prefetch((const char*)&output_surface[src + x + 16], _MM_HINT_T0);
                _mm_prefetch((const char*)&output_surface[src + x + 24], _MM_HINT_T0);

                // Load the pixels, 16-bit channels, 8 bytes per pixel, e.g
                // pixel01 = [AA AA BB BB GG GG RR RR AA AA BB BB GG GG RR RR
                auto pixel01 = _mm_load_si128((__m128i*)&output_surface[src + x + 0]);
                auto pixel23 = _mm_load_si128((__m128i*)&output_surface[src + x + 2]);
                auto pixel45 = _mm_load_si128((__m128i*)&output_surface[src + x + 4]);
                auto pixel67 = _mm_load_si128((__m128i*)&output_surface[src + x + 6]);
                auto pixel89 = _mm_load_si128((__m128i*)&output_surface[src + x + 8]);
                auto pixel1011 = _mm_load_si128((__m128i*)&output_surface[src + x + 10]);
                auto pixel1213 = _mm_load_si128((__m128i*)&output_surface[src + x + 12]);
                auto pixel1415 = _mm_load_si128((__m128i*)&output_surface[src + x + 14]);

                // Right-shift the channels by 16 to un-do the left shit on read and bring the range
                // back to 8-bit.
                pixel01 = _mm_srli_epi16(pixel01, 2);
                pixel23 = _mm_srli_epi16(pixel23, 2);
                pixel45 = _mm_srli_epi16(pixel45, 2);
                pixel67 = _mm_srli_epi16(pixel67, 2);
                pixel89 = _mm_srli_epi16(pixel89, 2);
                pixel1011 = _mm_srli_epi16(pixel1011, 2);
                pixel1213 = _mm_srli_epi16(pixel1213, 2);
                pixel1415 = _mm_srli_epi16(pixel1415, 2);

                // Pack with unsigned saturation 16-bit channels from 2 registers into 8-bit channels in 1 register.
                // pixel01    = [AA2 AA2] [BB2 BB2] [GG2 GG2] [RR2 RR2] [AA1 AA1] [BB1 BB1] [GG1 GG1] [RR1 RR1]
                // pixel23    = [AA4 AA4] [BB4 BB4] [GG4 GG4] [RR4 RR4] [AA3 AA3] [BB3 BB3] [GG3 GG3] [RR3 RR3]
                // ->
                // pixels0_lo = [AA4] [BB4] [GG4] [RR4] [AA3] [BB3] [GG3] [RR3] [AA2] [BB2] [GG2] [RR2] [AA1] [BB1] [GG1] [RR1]
                auto pixels0_lo = _mm_packus_epi16(pixel01, pixel23);
                auto pixels0_hi = _mm_packus_epi16(pixel45, pixel67);
                auto pixels1_lo = _mm_packus_epi16(pixel89, pixel1011);
                auto pixels1_hi = _mm_packus_epi16(pixel1213, pixel1415);

                if constexpr (Format == VideoPixelFormat::A8R8G8B8) {
                    const auto shuffle =
                        _mm_set_epi8(15, 12, 13, 14, 11, 8, 9, 10, 7, 4, 5, 6, 3, 0, 1, 2);

                    // Our pixels are ABGR (big-endian) by default, if ARGB is needed, we need to shuffle.
                    // pixels0_lo = [AA4 BB4 GG4 RR4] [AA3 BB3 GG3 RR3] [AA2 BB2 GG2 RR2] [AA1 BB1 GG1 RR1]
                    // ->
                    // pixels0_lo = [AA4 RR4 GG4 BB4] [AA3 RR3 GG3 BB3] [AA2 RR2 GG2 BB2] [AA1 RR1 GG1 BB1]
                    pixels0_lo = _mm_shuffle_epi8(pixels0_lo, shuffle);
                    pixels0_hi = _mm_shuffle_epi8(pixels0_hi, shuffle);
                    pixels1_lo = _mm_shuffle_epi8(pixels1_lo, shuffle);
                    pixels1_hi = _mm_shuffle_epi8(pixels1_hi, shuffle);
                }

                // Store the pixels
                _mm_store_si128((__m128i*)&out_buffer[dst + x * 4 + 0], pixels0_lo);
                _mm_store_si128((__m128i*)&out_buffer[dst + x * 4 + 16], pixels0_hi);
                _mm_store_si128((__m128i*)&out_buffer[dst + x * 4 + 32], pixels1_lo);
                _mm_store_si128((__m128i*)&out_buffer[dst + x * 4 + 48], pixels1_hi);

                // clang-format on
            }

            for (; x < surface_width; x++) {
                if constexpr (Format == VideoPixelFormat::A8R8G8B8) {
                    out_buffer[dst + x * 4 + 0] = static_cast<u8>(output_surface[src + x].b >> 2);
                    out_buffer[dst + x * 4 + 1] = static_cast<u8>(output_surface[src + x].g >> 2);
                    out_buffer[dst + x * 4 + 2] = static_cast<u8>(output_surface[src + x].r >> 2);
                    out_buffer[dst + x * 4 + 3] = static_cast<u8>(output_surface[src + x].a >> 2);
                } else {
                    out_buffer[dst + x * 4 + 0] = static_cast<u8>(output_surface[src + x].r >> 2);
                    out_buffer[dst + x * 4 + 1] = static_cast<u8>(output_surface[src + x].g >> 2);
                    out_buffer[dst + x * 4 + 2] = static_cast<u8>(output_surface[src + x].b >> 2);
                    out_buffer[dst + x * 4 + 3] = static_cast<u8>(output_surface[src + x].a >> 2);
                }
            }
        }
#else
        DecodeLinear(out_buffer);
#endif
    };

    switch (output_surface_config.out_block_kind) {
    case BLK_KIND::GENERIC_16Bx2: {
        const u32 block_height = static_cast<u32>(output_surface_config.out_block_height);
        const auto out_swizzle_size = Texture::CalculateSize(true, BytesPerPixel, out_luma_width,
                                                             out_luma_height, 1, block_height, 0);

        LOG_TRACE(
            HW_GPU,
            "Writing ABGR swizzled frame\n"
            "\tinput surface {}x{} stride {} size 0x{:X}\n"
            "\toutput surface {}x{} stride {} size 0x{:X} block height {} swizzled size 0x{:X}",
            surface_width, surface_height, surface_stride * BytesPerPixel,
            surface_stride * surface_height * BytesPerPixel, out_luma_width, out_luma_height,
            out_luma_stride, out_luma_size, block_height, out_swizzle_size);

        luma_scratch.resize_destructive(out_luma_size);

        Decode(luma_scratch);

        Tegra::Memory::GpuGuestMemoryScoped<u8, Core::Memory::GuestMemoryFlags::SafeWrite> out_luma(
            memory_manager, regs.output_surface.luma.Address(), out_swizzle_size, &swizzle_scratch);

        if (block_height == 1) {
            SwizzleSurface(out_luma, out_luma_stride, luma_scratch, out_luma_stride,
                           out_luma_height);
        } else {
            Texture::SwizzleTexture(out_luma, luma_scratch, BytesPerPixel, out_luma_width,
                                    out_luma_height, 1, block_height, 0, 1);
        }

    } break;
    case BLK_KIND::PITCH: {
        LOG_TRACE(HW_GPU,
                  "Writing ABGR pitch frame\n"
                  "\tinput surface {}x{} stride {} size 0x{:X}"
                  "\toutput surface {}x{} stride {} size 0x{:X}",
                  surface_width, surface_height, surface_stride,
                  surface_stride * surface_height * BytesPerPixel, out_luma_width, out_luma_height,
                  out_luma_stride, out_luma_size);

        luma_scratch.resize_destructive(out_luma_size);

        Tegra::Memory::GpuGuestMemoryScoped<u8, Core::Memory::GuestMemoryFlags::SafeWrite> out_luma(
            memory_manager, regs.output_surface.luma.Address(), out_luma_size, &luma_scratch);

        Decode(out_luma);
    } break;
    default:
        UNREACHABLE();
        break;
    }
}

} // namespace Tegra::Host1x
