// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"

#include "common/polyfill_thread.h"
#include "common/settings.h"
#include "video_core/host1x/codecs/h264.h"
#include "video_core/host1x/codecs/vp8.h"
#include "video_core/host1x/codecs/vp9.h"
#include "video_core/host1x/host1x.h"
#include "video_core/host1x/nvdec.h"

namespace Tegra::Host1x {

#define NVDEC_REG_INDEX(field_name)                                                                \
    (offsetof(NvdecCommon::NvdecRegisters, field_name) / sizeof(u64))

Nvdec::Nvdec(Host1x& host1x_, s32 id_, u32 syncpt, FrameQueue& frame_queue_)
    : CDmaPusher{host1x_, id_}, id{id_}, syncpoint{syncpt}, frame_queue{frame_queue_} {
    LOG_INFO(HW_GPU, "Created nvdec {}", id);
    frame_queue.Open(id);
}

Nvdec::~Nvdec() {
    LOG_INFO(HW_GPU, "Destroying nvdec {}", id);
}

void Nvdec::ProcessMethod(u32 method, u32 argument) {
    regs.reg_array[method] = argument;

    switch (method) {
    case NVDEC_REG_INDEX(set_codec_id):
        CreateDecoder(static_cast<NvdecCommon::VideoCodec>(argument));
        break;
    case NVDEC_REG_INDEX(execute): {
        if (wait_needed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(32));
            wait_needed = false;
        }
        Execute();
    } break;
    }
}

void Nvdec::CreateDecoder(NvdecCommon::VideoCodec codec) {
    if (decoder.get()) {
        return;
    }
    switch (codec) {
    case NvdecCommon::VideoCodec::H264:
        decoder = std::make_unique<Decoders::H264>(host1x, regs, id, frame_queue);
        break;
    case NvdecCommon::VideoCodec::VP8:
        decoder = std::make_unique<Decoders::VP8>(host1x, regs, id, frame_queue);
        break;
    case NvdecCommon::VideoCodec::VP9:
        decoder = std::make_unique<Decoders::VP9>(host1x, regs, id, frame_queue);
        break;
    default:
        UNIMPLEMENTED_MSG("Codec {}", decoder->GetCurrentCodecName());
        break;
    }
    LOG_INFO(HW_GPU, "Created decoder {} for id {}", decoder->GetCurrentCodecName(), id);
}

void Nvdec::Execute() {
    if (Settings::values.nvdec_emulation.GetValue() == Settings::NvdecEmulation::Off) [[unlikely]] {
        // Signalling syncpts too fast can cause games to get stuck as they don't expect a <1ms
        // execution time. Sleep for half of a 60 fps frame just in case.
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
        return;
    }
    switch (decoder->GetCurrentCodec()) {
    case NvdecCommon::VideoCodec::H264:
    case NvdecCommon::VideoCodec::VP8:
    case NvdecCommon::VideoCodec::VP9:
        decoder->Decode();
        break;
    default:
        UNIMPLEMENTED_MSG("Codec {}", decoder->GetCurrentCodecName());
        break;
    }
}

} // namespace Tegra::Host1x
