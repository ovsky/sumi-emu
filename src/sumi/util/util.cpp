// SPDX-FileCopyrightText: 2015 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cmath>
#include <QPainter>

#include "common/logging/log.h"
#include "sumi/util/util.h"

#ifdef _WIN32
#include <windows.h>
#include "common/fs/file.h"
#endif

QFont GetMonospaceFont() {
    QFont font(QStringLiteral("monospace"));
    // Automatic fallback to a monospace font on on platforms without a font called "monospace"
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

QString ReadableByteSize(qulonglong size) {
    static constexpr std::array units{"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    if (size == 0) {
        return QStringLiteral("0");
    }

    const int digit_groups = std::min(static_cast<int>(std::log10(size) / std::log10(1024)),
                                      static_cast<int>(units.size()));
    return QStringLiteral("%L1 %2")
        .arg(size / std::pow(1024, digit_groups), 0, 'f', 1)
        .arg(QString::fromUtf8(units[digit_groups]));
}

QPixmap CreateCirclePixmapFromColor(const QColor& color) {
    QPixmap circle_pixmap(16, 16);
    circle_pixmap.fill(Qt::transparent);
    QPainter painter(&circle_pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(color);
    painter.setBrush(color);
    painter.drawEllipse({circle_pixmap.width() / 2.0, circle_pixmap.height() / 2.0}, 7.0, 7.0);
    return circle_pixmap;
}

bool SaveIconToFile(const std::filesystem::path& icon_path, const QImage& image) {
#if defined(WIN32)
#pragma pack(push, 2)
    struct IconDir {
        WORD id_reserved;
        WORD id_type;
        WORD id_count;
    };

    struct IconDirEntry {
        BYTE width;
        BYTE height;
        BYTE color_count;
        BYTE reserved;
        WORD planes;
        WORD bit_count;
        DWORD bytes_in_res;
        DWORD image_offset;
    };
#pragma pack(pop)

    const QImage source_image = image.convertToFormat(QImage::Format_RGB32);
    constexpr std::array<int, 7> scale_sizes{256, 128, 64, 48, 32, 24, 16};
    constexpr int bytes_per_pixel = 4;

    const IconDir icon_dir{
        .id_reserved = 0,
        .id_type = 1,
        .id_count = static_cast<WORD>(scale_sizes.size()),
    };

    Common::FS::IOFile icon_file(icon_path.string(), Common::FS::FileAccessMode::Write,
                                 Common::FS::FileType::BinaryFile);
    if (!icon_file.IsOpen()) {
        return false;
    }

    if (!icon_file.Write(icon_dir)) {
        return false;
    }

    std::size_t image_offset = sizeof(IconDir) + (sizeof(IconDirEntry) * scale_sizes.size());
    for (std::size_t i = 0; i < scale_sizes.size(); i++) {
        const int image_size = scale_sizes[i] * scale_sizes[i] * bytes_per_pixel;
        const IconDirEntry icon_entry{
            .width = static_cast<BYTE>(scale_sizes[i]),
            .height = static_cast<BYTE>(scale_sizes[i]),
            .color_count = 0,
            .reserved = 0,
            .planes = 1,
            .bit_count = bytes_per_pixel * 8,
            .bytes_in_res = static_cast<DWORD>(sizeof(BITMAPINFOHEADER) + image_size),
            .image_offset = static_cast<DWORD>(image_offset),
        };
        image_offset += icon_entry.bytes_in_res;
        if (!icon_file.Write(icon_entry)) {
            return false;
        }
    }

    for (std::size_t i = 0; i < scale_sizes.size(); i++) {
        const QImage scaled_image = source_image.scaled(
            scale_sizes[i], scale_sizes[i], Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        const BITMAPINFOHEADER info_header{
            .biSize = sizeof(BITMAPINFOHEADER),
            .biWidth = scaled_image.width(),
            .biHeight = scaled_image.height() * 2,
            .biPlanes = 1,
            .biBitCount = bytes_per_pixel * 8,
            .biCompression = BI_RGB,
            .biSizeImage{},
            .biXPelsPerMeter{},
            .biYPelsPerMeter{},
            .biClrUsed{},
            .biClrImportant{},
        };

        if (!icon_file.Write(info_header)) {
            return false;
        }

        for (int y = 0; y < scaled_image.height(); y++) {
            const auto* line = scaled_image.scanLine(scaled_image.height() - 1 - y);
            std::vector<u8> line_data(scaled_image.width() * bytes_per_pixel);
            std::memcpy(line_data.data(), line, line_data.size());
            if (!icon_file.Write(line_data)) {
                return false;
            }
        }
    }
    icon_file.Close();

    return true;
#elif defined(__linux__) || defined(__FreeBSD__)
    // Convert and write the icon as a PNG
    if (!image.save(QString::fromStdString(icon_path.string()))) {
        LOG_ERROR(Frontend, "Could not write icon as PNG to file");
    } else {
        LOG_INFO(Frontend, "Wrote an icon to {}", icon_path.string());
    }
    return true;
#else
    return false;
#endif
}
