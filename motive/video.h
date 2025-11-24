#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>

#include "model.h"

namespace glyph {
struct OverlayBitmap;
}

extern "C" {
struct AVFormatContext;
struct AVCodecContext;
struct SwsContext;
struct AVFrame;
struct AVPacket;
}

#include <libavutil/pixfmt.h>

namespace video {

struct VideoDecoder {
    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    SwsContext* swsCtx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* nv12Frame = nullptr;
    AVPacket* packet = nullptr;
    int videoStreamIndex = -1;
    int width = 0;
    int height = 0;
    int bufferSize = 0;
    int yPlaneSize = 0;
    int uvPlaneSize = 0;
    AVColorSpace colorSpace = AVCOL_SPC_UNSPECIFIED;
    AVColorRange colorRange = AVCOL_RANGE_UNSPECIFIED;
    double fps = 30.0;
    bool draining = false;
    std::atomic<bool> finished{false};
    // Async decoding
    std::thread decodeThread;
    std::mutex frameMutex;
    std::condition_variable frameCond;
    std::deque<std::vector<uint8_t>> frameQueue;
    size_t maxBufferedFrames = 12;
    bool asyncDecoding = false;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> threadRunning{false};
};

std::optional<std::filesystem::path> locateVideoFile(const std::string& filename);
bool initializeVideoDecoder(const std::filesystem::path& videoPath, VideoDecoder& decoder);
bool decodeNextFrame(VideoDecoder& decoder, std::vector<uint8_t>& rgbaBuffer);
bool startAsyncDecoding(VideoDecoder& decoder, size_t maxBufferedFrames = 12);
bool acquireDecodedFrame(VideoDecoder& decoder, std::vector<uint8_t>& rgbaBuffer);
void stopAsyncDecoding(VideoDecoder& decoder);
void cleanupVideoDecoder(VideoDecoder& decoder);
enum class VideoColorSpace : uint32_t {
    BT601 = 0,
    BT709 = 1,
    BT2020 = 2
};

enum class VideoColorRange : uint32_t {
    Limited = 0,
    Full = 1
};

struct VideoColorInfo {
    VideoColorSpace colorSpace = VideoColorSpace::BT709;
    VideoColorRange colorRange = VideoColorRange::Limited;
};

struct Nv12Overlay {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t offsetX = 0;
    uint32_t offsetY = 0;
    uint32_t uvWidth = 0;
    uint32_t uvHeight = 0;
    std::vector<uint8_t> yPlane;
    std::vector<uint8_t> yAlpha;
    std::vector<uint8_t> uvPlane;
    std::vector<uint8_t> uvAlpha;

    bool isValid() const
    {
        return width > 0 && height > 0 && !yPlane.empty() && !uvPlane.empty();
    }
};

VideoColorInfo deriveVideoColorInfo(const VideoDecoder& decoder);
Nv12Overlay convertOverlayToNv12(const glyph::OverlayBitmap& bitmap, const VideoColorInfo& colorInfo);
void applyNv12Overlay(std::vector<uint8_t>& nv12Buffer,
                      uint32_t frameWidth,
                      uint32_t frameHeight,
                      const Nv12Overlay& overlay);
std::vector<Vertex> buildVideoQuadVertices(float width, float height);

} // namespace video
