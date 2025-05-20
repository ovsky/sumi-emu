// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>

#include "common/common_types.h"
#include "video_core/cdma_pusher.h"
#include "video_core/host1x/codecs/decoder.h"

namespace Tegra {

namespace Host1x {
class Host1x;
class FrameQueue;

class Nvdec final : public CDmaPusher {
public:
    explicit Nvdec(Host1x& host1x, s32 id, u32 syncpt, FrameQueue& frame_queue_);
    ~Nvdec();

    /// Writes the method into the state, Invoke Execute() if encountered
    void ProcessMethod(u32 method, u32 arg) override;

    u32 GetSyncpoint() const {
        return syncpoint;
    }

    void SetWait() {
        wait_needed = true;
    }

private:
    /// Create the decoder when the codec id is set
    void CreateDecoder(NvdecCommon::VideoCodec codec);

    /// Invoke codec to decode a frame
    void Execute();

    s32 id;
    u32 syncpoint;
    FrameQueue& frame_queue;

    NvdecCommon::NvdecRegisters regs{};
    std::unique_ptr<Decoder> decoder;
    bool wait_needed{false};
};

} // namespace Host1x

} // namespace Tegra
