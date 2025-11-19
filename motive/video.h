#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "model.h"

extern "C" {
struct AVFormatContext;
struct AVCodecContext;
struct SwsContext;
struct AVFrame;
struct AVPacket;
}

namespace video {

struct VideoDecoder {
    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    SwsContext* swsCtx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* rgbaFrame = nullptr;
    AVPacket* packet = nullptr;
    int videoStreamIndex = -1;
    int width = 0;
    int height = 0;
    int bufferSize = 0;
    double fps = 30.0;
    bool draining = false;
    bool finished = false;
};

std::optional<std::filesystem::path> locateVideoFile(const std::string& filename);
bool initializeVideoDecoder(const std::filesystem::path& videoPath, VideoDecoder& decoder);
bool decodeNextFrame(VideoDecoder& decoder, std::vector<uint8_t>& rgbaBuffer);
void cleanupVideoDecoder(VideoDecoder& decoder);
std::vector<Vertex> buildVideoQuadVertices(float width, float height);

} // namespace video
