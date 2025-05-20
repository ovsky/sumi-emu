// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 suyu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/memory.h"
#include "video_core/host1x/ffmpeg/ffmpeg.h"
#include "video_core/memory_manager.h"

extern "C" {
#ifdef LIBVA_FOUND
// for querying VAAPI driver information
#include <libavutil/hwcontext_vaapi.h>
#endif
}

namespace FFmpeg {

namespace {

constexpr AVPixelFormat PreferredGpuFormat = AV_PIX_FMT_NV12;
constexpr AVPixelFormat PreferredCpuFormat = AV_PIX_FMT_YUV420P;
constexpr std::array PreferredGpuDecoders = {
    AV_HWDEVICE_TYPE_CUDA,
#ifdef _WIN32
    AV_HWDEVICE_TYPE_D3D11VA,
    AV_HWDEVICE_TYPE_DXVA2,
#elif defined(__unix__)
    AV_HWDEVICE_TYPE_VAAPI,
    AV_HWDEVICE_TYPE_VDPAU,
#endif
    // last resort for Linux Flatpak (w/ NVIDIA)
    AV_HWDEVICE_TYPE_VULKAN,
};

AVPixelFormat GetGpuFormat(AVCodecContext* codec_context, const AVPixelFormat* pix_fmts) {
    for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == codec_context->pix_fmt) {
            return codec_context->pix_fmt;
        }
    }

    LOG_INFO(HW_GPU, "Could not find compatible GPU AV format, falling back to CPU");
    av_buffer_unref(&codec_context->hw_device_ctx);

    codec_context->pix_fmt = PreferredCpuFormat;
    return codec_context->pix_fmt;
}

std::string AVError(int errnum)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_make_error_string(errbuf, sizeof(errbuf) - 1, errnum);
    return errbuf;
}

} // namespace

Packet::Packet(std::span<const u8> data) {
    m_packet = av_packet_alloc();
    m_packet->data = const_cast<u8*>(data.data());
    m_packet->size = static_cast<s32>(data.size());
}

Packet::~Packet() {
    av_packet_free(&m_packet);
}

Frame::Frame() {
    m_frame = av_frame_alloc();
}

Frame::~Frame() {
    av_frame_free(&m_frame);
}

Decoder::Decoder(Tegra::Host1x::NvdecCommon::VideoCodec codec) {
    const AVCodecID av_codec = [&] {
        switch (codec) {
        case Tegra::Host1x::NvdecCommon::VideoCodec::H264:
            return AV_CODEC_ID_H264;
        case Tegra::Host1x::NvdecCommon::VideoCodec::VP8:
            return AV_CODEC_ID_VP8;
        case Tegra::Host1x::NvdecCommon::VideoCodec::VP9:
            return AV_CODEC_ID_VP9;
        default:
            UNIMPLEMENTED_MSG("Unknown codec {}", codec);
            return AV_CODEC_ID_NONE;
        }
    }();

    m_codec = avcodec_find_decoder(av_codec);
}

bool Decoder::SupportsDecodingOnDevice(AVPixelFormat* out_pix_fmt, AVHWDeviceType type) const {
    for (int i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(m_codec, i);
        if (!config) {
            LOG_DEBUG(HW_GPU, "{} decoder does not support device type {}", m_codec->name,
                      av_hwdevice_get_type_name(type));
            break;
        }
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0 &&
            config->device_type == type) {
            LOG_INFO(HW_GPU, "Using {} GPU decoder", av_hwdevice_get_type_name(type));
            *out_pix_fmt = config->pix_fmt;
            return true;
        }
    }

    return false;
}

std::vector<AVHWDeviceType> HardwareContext::GetSupportedDeviceTypes() {
    std::vector<AVHWDeviceType> types;
    AVHWDeviceType current_device_type = AV_HWDEVICE_TYPE_NONE;

    while (true) {
        current_device_type = av_hwdevice_iterate_types(current_device_type);
        if (current_device_type == AV_HWDEVICE_TYPE_NONE) {
            return types;
        }

        types.push_back(current_device_type);
    }
}

HardwareContext::~HardwareContext() {
    av_buffer_unref(&m_gpu_decoder);
}

bool HardwareContext::InitializeForDecoder(DecoderContext& decoder_context,
                                           const Decoder& decoder) {
    const auto supported_types = GetSupportedDeviceTypes();
    for (const auto type : PreferredGpuDecoders) {
        AVPixelFormat hw_pix_fmt;

        if (std::ranges::find(supported_types, type) == supported_types.end()) {
            LOG_DEBUG(HW_GPU, "{} explicitly unsupported", av_hwdevice_get_type_name(type));
            continue;
        }

        if (!this->InitializeWithType(type)) {
            continue;
        }

        if (decoder.SupportsDecodingOnDevice(&hw_pix_fmt, type)) {
            decoder_context.InitializeHardwareDecoder(*this, hw_pix_fmt);
            return true;
        }
    }

    LOG_INFO(HW_GPU, "Hardware decoding is disabled due to implementation issues, using CPU.");
    return false;
}

bool HardwareContext::InitializeWithType(AVHWDeviceType type) {
    av_buffer_unref(&m_gpu_decoder);

    if (const int ret = av_hwdevice_ctx_create(&m_gpu_decoder, type, nullptr, nullptr, 0);
        ret < 0) {
        LOG_DEBUG(HW_GPU, "av_hwdevice_ctx_create({}) failed: {}", av_hwdevice_get_type_name(type),
                  AVError(ret));
        return false;
    }

#ifdef LIBVA_FOUND
    if (type == AV_HWDEVICE_TYPE_VAAPI) {
        // We need to determine if this is an impersonated VAAPI driver.
        auto* hwctx = reinterpret_cast<AVHWDeviceContext*>(m_gpu_decoder->data);
        auto* vactx = static_cast<AVVAAPIDeviceContext*>(hwctx->hwctx);
        const char* vendor_name = vaQueryVendorString(vactx->display);
        if (strstr(vendor_name, "VDPAU backend")) {
            // VDPAU impersonated VAAPI impls are super buggy, we need to skip them.
            LOG_DEBUG(HW_GPU, "Skipping VDPAU impersonated VAAPI driver");
            return false;
        } else {
            // According to some user testing, certain VAAPI drivers (Intel?) could be buggy.
            // Log the driver name just in case.
            LOG_DEBUG(HW_GPU, "Using VAAPI driver: {}", vendor_name);
        }
    }
#endif

    return true;
}

DecoderContext::DecoderContext(const Decoder& decoder) : m_decoder{decoder} {
    m_codec_context = avcodec_alloc_context3(m_decoder.GetCodec());
    av_opt_set(m_codec_context->priv_data, "tune", "zerolatency", 0);
    m_codec_context->thread_count = 0;
    m_codec_context->thread_type &= ~FF_THREAD_FRAME;
}

DecoderContext::~DecoderContext() {
    av_buffer_unref(&m_codec_context->hw_device_ctx);
    avcodec_free_context(&m_codec_context);
}

void DecoderContext::InitializeHardwareDecoder(const HardwareContext& context,
                                               AVPixelFormat hw_pix_fmt) {
    m_codec_context->hw_device_ctx = av_buffer_ref(context.GetBufferRef());
    m_codec_context->get_format = GetGpuFormat;
    m_codec_context->pix_fmt = hw_pix_fmt;
}

bool DecoderContext::OpenContext(const Decoder& decoder) {
    if (const int ret = avcodec_open2(m_codec_context, decoder.GetCodec(), nullptr); ret < 0) {
        LOG_ERROR(HW_GPU, "avcodec_open2 error: {}", AVError(ret));
        return false;
    }

    if (!m_codec_context->hw_device_ctx) {
        LOG_INFO(HW_GPU, "Using FFmpeg software decoding");
    }

    return true;
}

// Nasty but allows linux builds to pass.
// Requires double checks when FFMPEG gets updated.
// Hopefully a future FFMPEG update will all and expose a solution in the public API.
namespace {

typedef struct FFCodecDefault {
    const char* key;
    const char* value;
} FFCodecDefault;

typedef struct FFCodec {
    /**
     * The public AVCodec. See codec.h for it.
     */
    AVCodec p;

    /**
     * Internal codec capabilities FF_CODEC_CAP_*.
     */
    unsigned caps_internal : 29;

    /**
     * This field determines the type of the codec (decoder/encoder)
     * and also the exact callback cb implemented by the codec.
     * cb_type uses enum FFCodecType values.
     */
    unsigned cb_type : 3;

    int priv_data_size;
    /**
     * @name Frame-level threading support functions
     * @{
     */
    /**
     * Copy necessary context variables from a previous thread context to the current one.
     * If not defined, the next thread will start automatically; otherwise, the codec
     * must call ff_thread_finish_setup().
     *
     * dst and src will (rarely) point to the same context, in which case memcpy should be skipped.
     */
    int (*update_thread_context)(struct AVCodecContext* dst, const struct AVCodecContext* src);

    /**
     * Copy variables back to the user-facing context
     */
    int (*update_thread_context_for_user)(struct AVCodecContext* dst,
                                          const struct AVCodecContext* src);
    /** @} */

    /**
     * Private codec-specific defaults.
     */
    const FFCodecDefault* defaults;

    /**
     * Initialize codec static data, called from av_codec_iterate().
     *
     * This is not intended for time consuming operations as it is
     * run for every codec regardless of that codec being used.
     */
    void (*init_static_data)(struct FFCodec* codec);

    int (*init)(struct AVCodecContext*);

    union {
        /**
         * Decode to an AVFrame.
         * cb is in this state if cb_type is FF_CODEC_CB_TYPE_DECODE.
         *
         * @param      avctx          codec context
         * @param[out] frame          AVFrame for output
         * @param[out] got_frame_ptr  decoder sets to 0 or 1 to indicate that
         *                            a non-empty frame was returned in frame.
         * @param[in]  avpkt          AVPacket containing the data to be decoded
         * @return amount of bytes read from the packet on success,
         *         negative error code on failure
         */
        int (*decode)(struct AVCodecContext* avctx, struct AVFrame* frame, int* got_frame_ptr,
                      struct AVPacket* avpkt);
        /**
         * Decode subtitle data to an AVSubtitle.
         * cb is in this state if cb_type is FF_CODEC_CB_TYPE_DECODE_SUB.
         *
         * Apart from that this is like the decode callback.
         */
        int (*decode_sub)(struct AVCodecContext* avctx, struct AVSubtitle* sub, int* got_frame_ptr,
                          const struct AVPacket* avpkt);
        /**
         * Decode API with decoupled packet/frame dataflow.
         * cb is in this state if cb_type is FF_CODEC_CB_TYPE_RECEIVE_FRAME.
         *
         * This function is called to get one output frame. It should call
         * ff_decode_get_packet() to obtain input data.
         */
        int (*receive_frame)(struct AVCodecContext* avctx, struct AVFrame* frame);
        /**
         * Encode data to an AVPacket.
         * cb is in this state if cb_type is FF_CODEC_CB_TYPE_ENCODE
         *
         * @param      avctx          codec context
         * @param[out] avpkt          output AVPacket
         * @param[in]  frame          AVFrame containing the input to be encoded
         * @param[out] got_packet_ptr encoder sets to 0 or 1 to indicate that a
         *                            non-empty packet was returned in avpkt.
         * @return 0 on success, negative error code on failure
         */
        int (*encode)(struct AVCodecContext* avctx, struct AVPacket* avpkt,
                      const struct AVFrame* frame, int* got_packet_ptr);
        /**
         * Encode subtitles to a raw buffer.
         * cb is in this state if cb_type is FF_CODEC_CB_TYPE_ENCODE_SUB.
         */
        int (*encode_sub)(struct AVCodecContext* avctx, uint8_t* buf, int buf_size,
                          const struct AVSubtitle* sub);
        /**
         * Encode API with decoupled frame/packet dataflow.
         * cb is in this state if cb_type is FF_CODEC_CB_TYPE_RECEIVE_PACKET.
         *
         * This function is called to get one output packet.
         * It should call ff_encode_get_frame() to obtain input data.
         */
        int (*receive_packet)(struct AVCodecContext* avctx, struct AVPacket* avpkt);
    } cb;

    int (*close)(struct AVCodecContext*);

    /**
     * Flush buffers.
     * Will be called when seeking
     */
    void (*flush)(struct AVCodecContext*);

    /**
     * Decoding only, a comma-separated list of bitstream filters to apply to
     * packets before decoding.
     */
    const char* bsfs;

    /**
     * Array of pointers to hardware configurations supported by the codec,
     * or NULL if no hardware supported.  The array is terminated by a NULL
     * pointer.
     *
     * The user can only access this field via avcodec_get_hw_config().
     */
    const struct AVCodecHWConfigInternal* const* hw_configs;

    /**
     * List of supported codec_tags, terminated by FF_CODEC_TAGS_END.
     */
    const uint32_t* codec_tags;
} FFCodec;

#ifndef ANDROID
static av_always_inline const FFCodec* ffcodec(const AVCodec* codec) {
    return (const FFCodec*)codec;
}
#endif
} // namespace


bool DecoderContext::SendPacket(const Packet& packet) {
    m_temp_frame = std::make_shared<Frame>();
    m_got_frame = 0;

// Android can randomly crash when calling decode directly, so skip.
// TODO update ffmpeg and hope that fixes it.
#ifndef ANDROID
    if (!m_codec_context->hw_device_ctx && m_codec_context->codec_id == AV_CODEC_ID_H264) {
        m_decode_order = true;
        auto* codec{ffcodec(m_decoder.GetCodec())};
        if (const int ret = codec->cb.decode(m_codec_context,
                                             m_temp_frame->GetFrame(),
                                             &m_got_frame,
                                             packet.GetPacket());
            ret < 0) {
            LOG_DEBUG(Service_NVDRV, "avcodec_send_packet error {}", AVError(ret));
            return false;
        }
        return true;
    }
#endif

    if (const int ret = avcodec_send_packet(m_codec_context, packet.GetPacket()); ret < 0) {
        LOG_ERROR(HW_GPU, "avcodec_send_packet error: {}", AVError(ret));
        return false;
    }

    return true;
}

std::shared_ptr<Frame> DecoderContext::ReceiveFrame() {
    // Android can randomly crash when calling decode directly, so skip.
    // TODO update ffmpeg and hope that fixes it.
// TODO: This is causing issues on linux, need to bisect
#ifndef ANDROID
    if (!m_codec_context->hw_device_ctx && m_codec_context->codec_id == AV_CODEC_ID_H264) {
        m_decode_order = true;
        auto* codec{ffcodec(m_decoder.GetCodec())};
        int ret{0};

        if (m_got_frame == 0) {
            Packet packet{{}};
            auto* pkt = packet.GetPacket();
            pkt->data = nullptr;
            pkt->size = 0;
            ret = codec->cb.decode(m_codec_context, m_temp_frame->GetFrame(), &m_got_frame, pkt);
            m_codec_context->has_b_frames = 0;
        }

        if (m_got_frame == 0 || ret < 0) {
            LOG_ERROR(Service_NVDRV, "Failed to receive a frame! error {}", ret);
            return {};
        }
    } else
#endif
    {

        const auto ReceiveImpl = [&](AVFrame* frame) {
            if (const int ret = avcodec_receive_frame(m_codec_context, frame); ret < 0) {
                LOG_ERROR(HW_GPU, "avcodec_receive_frame error: {}", AVError(ret));
                return false;
            }

            return true;
        };

        if (m_codec_context->hw_device_ctx) {
            // If we have a hardware context, make a separate frame here to receive the
            // hardware result before sending it to the output.
            Frame intermediate_frame;

            if (!ReceiveImpl(intermediate_frame.GetFrame())) {
                return {};
            }

            m_temp_frame->SetFormat(PreferredGpuFormat);
            if (const int ret = av_hwframe_transfer_data(m_temp_frame->GetFrame(),
                                                         intermediate_frame.GetFrame(), 0);
                ret < 0) {
                LOG_ERROR(HW_GPU, "av_hwframe_transfer_data error: {}", AVError(ret));
                return {};
            }
        } else {
            // Otherwise, decode the frame as normal.
            if (!ReceiveImpl(m_temp_frame->GetFrame())) {
                return {};
            }
        }
    }

#if defined(FF_API_INTERLACED_FRAME) || LIBAVUTIL_VERSION_MAJOR >= 59
    m_temp_frame->GetFrame()->interlaced_frame =
        (m_temp_frame->GetFrame()->flags & AV_FRAME_FLAG_INTERLACED) != 0;
#endif
    return std::move(m_temp_frame);
}

void DecodeApi::Reset() {
    m_hardware_context.reset();
    m_decoder_context.reset();
    m_decoder.reset();
}

bool DecodeApi::Initialize(Tegra::Host1x::NvdecCommon::VideoCodec codec) {
    this->Reset();
    m_decoder.emplace(codec);
    m_decoder_context.emplace(*m_decoder);

    // Enable GPU decoding if requested.
    if (Settings::values.nvdec_emulation.GetValue() == Settings::NvdecEmulation::Gpu) {
        m_hardware_context.emplace();
        m_hardware_context->InitializeForDecoder(*m_decoder_context, *m_decoder);
    }

    // Open the decoder context.
    if (!m_decoder_context->OpenContext(*m_decoder)) {
        this->Reset();
        return false;
    }

    return true;
}

bool DecodeApi::SendPacket(std::span<const u8> packet_data) {
    FFmpeg::Packet packet(packet_data);
    return m_decoder_context->SendPacket(packet);
}

std::shared_ptr<Frame> DecodeApi::ReceiveFrame() {
    // Receive raw frame from decoder.
    return m_decoder_context->ReceiveFrame();
}

} // namespace FFmpeg
