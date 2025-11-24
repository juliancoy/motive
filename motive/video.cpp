#include "video.h"
#include "engine.h"
#include "display.h"
#include "camera.h"
#include "model.h"
#include "light.h"
#include "utils.h"
#include "glyph.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>
#include <system_error>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}

namespace video {

bool initializeVideoDecoder(const std::filesystem::path& videoPath, VideoDecoder& decoder)
{
    if (avformat_open_input(&decoder.formatCtx, videoPath.string().c_str(), nullptr, nullptr) < 0)
    {
        std::cerr << "[Video] Failed to open file: " << videoPath << std::endl;
        return false;
    }

    if (avformat_find_stream_info(decoder.formatCtx, nullptr) < 0)
    {
        std::cerr << "[Video] Unable to read stream info for: " << videoPath << std::endl;
        return false;
    }

    decoder.videoStreamIndex = av_find_best_stream(decoder.formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (decoder.videoStreamIndex < 0)
    {
        std::cerr << "[Video] No video stream found in file: " << videoPath << std::endl;
        return false;
    }

    AVStream* videoStream = decoder.formatCtx->streams[decoder.videoStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec)
    {
        std::cerr << "[Video] Decoder not found for stream." << std::endl;
        return false;
    }

    decoder.codecCtx = avcodec_alloc_context3(codec);
    if (!decoder.codecCtx)
    {
        std::cerr << "[Video] Failed to allocate codec context." << std::endl;
        return false;
    }

    if (avcodec_parameters_to_context(decoder.codecCtx, videoStream->codecpar) < 0)
    {
        std::cerr << "[Video] Unable to copy codec parameters." << std::endl;
        return false;
    }

    // Allow FFmpeg to spin multiple worker threads for decode if possible.
    unsigned int hwThreads = std::thread::hardware_concurrency();
    decoder.codecCtx->thread_count = hwThreads > 0 ? static_cast<int>(hwThreads) : 0;
    decoder.codecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    if (avcodec_open2(decoder.codecCtx, codec, nullptr) < 0)
    {
        std::cerr << "[Video] Failed to open codec." << std::endl;
        return false;
    }

    decoder.width = decoder.codecCtx->width;
    decoder.height = decoder.codecCtx->height;
    decoder.frame = av_frame_alloc();
    decoder.nv12Frame = av_frame_alloc();
    decoder.packet = av_packet_alloc();
    if (!decoder.frame || !decoder.nv12Frame || !decoder.packet)
    {
        std::cerr << "[Video] Failed to allocate FFmpeg structures." << std::endl;
        return false;
    }

    decoder.yPlaneSize = decoder.width * decoder.height;
    decoder.uvPlaneSize = decoder.yPlaneSize / 2;
    decoder.bufferSize = av_image_get_buffer_size(AV_PIX_FMT_NV12, decoder.width, decoder.height, 1);
    decoder.colorSpace = decoder.codecCtx->colorspace;
    decoder.colorRange = decoder.codecCtx->color_range;
    decoder.swsCtx = sws_getContext(decoder.width,
                                    decoder.height,
                                    decoder.codecCtx->pix_fmt,
                                    decoder.width,
                                    decoder.height,
                                    AV_PIX_FMT_NV12,
                                    SWS_BILINEAR,
                                    nullptr,
                                    nullptr,
                                    nullptr);
    if (!decoder.swsCtx)
    {
        std::cerr << "[Video] Failed to create sws context." << std::endl;
        return false;
    }

    AVRational frameRate = av_guess_frame_rate(decoder.formatCtx, videoStream, nullptr);
    double fps = (frameRate.num != 0 && frameRate.den != 0) ? av_q2d(frameRate) : 30.0;
    decoder.fps = fps > 0.0 ? fps : 30.0;
    return true;
}

bool decodeNextFrame(VideoDecoder& decoder, std::vector<uint8_t>& rgbaBuffer)
{
    if (decoder.finished.load())
    {
        return false;
    }

    while (true)
    {
        if (!decoder.draining)
        {
            int readResult = av_read_frame(decoder.formatCtx, decoder.packet);
            if (readResult >= 0)
            {
                if (decoder.packet->stream_index == decoder.videoStreamIndex)
                {
                    if (avcodec_send_packet(decoder.codecCtx, decoder.packet) < 0)
                    {
                        std::cerr << "[Video] Failed to send packet to decoder." << std::endl;
                    }
                }
                av_packet_unref(decoder.packet);
            }
            else
            {
                av_packet_unref(decoder.packet);
                decoder.draining = true;
                avcodec_send_packet(decoder.codecCtx, nullptr);
            }
        }

        int receiveResult = avcodec_receive_frame(decoder.codecCtx, decoder.frame);
        if (receiveResult == 0)
        {
            if (rgbaBuffer.size() != static_cast<size_t>(decoder.bufferSize))
            {
                rgbaBuffer.resize(decoder.bufferSize);
            }
            av_image_fill_arrays(decoder.nv12Frame->data,
                                 decoder.nv12Frame->linesize,
                                 rgbaBuffer.data(),
                                 AV_PIX_FMT_NV12,
                                 decoder.width,
                                 decoder.height,
                                 1);
            sws_scale(decoder.swsCtx,
                      decoder.frame->data,
                      decoder.frame->linesize,
                      0,
                      decoder.height,
                      decoder.nv12Frame->data,
                      decoder.nv12Frame->linesize);
            return true;
        }
        else if (receiveResult == AVERROR(EAGAIN))
        {
            continue;
        }
        else if (receiveResult == AVERROR_EOF)
        {
            decoder.finished.store(true);
            return false;
        }
        else
        {
            std::cerr << "[Video] Decoder error: " << receiveResult << std::endl;
            return false;
        }
    }
}

namespace
{
void asyncDecodeLoop(VideoDecoder* decoder)
{
    std::vector<uint8_t> localBuffer;
    localBuffer.reserve(decoder->bufferSize);
    while (!decoder->stopRequested.load())
    {
        if (!video::decodeNextFrame(*decoder, localBuffer))
        {
            break;
        }

        std::unique_lock<std::mutex> lock(decoder->frameMutex);
        decoder->frameCond.wait(lock, [decoder]() {
            return decoder->stopRequested.load() || decoder->frameQueue.size() < decoder->maxBufferedFrames;
        });

        if (decoder->stopRequested.load())
        {
            break;
        }

        decoder->frameQueue.emplace_back(std::move(localBuffer));
        lock.unlock();
        decoder->frameCond.notify_all();
        // Prepare buffer for reuse
        localBuffer.clear();
    }

    decoder->threadRunning.store(false);
    decoder->frameCond.notify_all();
}
} // namespace

bool startAsyncDecoding(VideoDecoder& decoder, size_t maxBufferedFrames)
{
    if (decoder.asyncDecoding)
    {
        return true;
    }

    decoder.maxBufferedFrames = std::max<size_t>(1, maxBufferedFrames);
    decoder.stopRequested.store(false);
    decoder.threadRunning.store(true);
    decoder.asyncDecoding = true;
    decoder.frameQueue.clear();

    try
    {
        decoder.decodeThread = std::thread(asyncDecodeLoop, &decoder);
    }
    catch (const std::system_error& err)
    {
        std::cerr << "[Video] Failed to start decode thread: " << err.what() << std::endl;
        decoder.threadRunning.store(false);
        decoder.asyncDecoding = false;
        return false;
    }

    return true;
}

bool acquireDecodedFrame(VideoDecoder& decoder, std::vector<uint8_t>& rgbaBuffer)
{
    std::unique_lock<std::mutex> lock(decoder.frameMutex);
    if (decoder.frameQueue.empty())
    {
        return false;
    }

    rgbaBuffer = std::move(decoder.frameQueue.front());
    decoder.frameQueue.pop_front();
    lock.unlock();
    decoder.frameCond.notify_all();
    return true;
}

void stopAsyncDecoding(VideoDecoder& decoder)
{
    if (!decoder.asyncDecoding)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(decoder.frameMutex);
        decoder.stopRequested.store(true);
    }
    decoder.frameCond.notify_all();

    if (decoder.decodeThread.joinable())
    {
        decoder.decodeThread.join();
    }

    decoder.frameQueue.clear();
    decoder.asyncDecoding = false;
    decoder.threadRunning.store(false);
    decoder.stopRequested.store(false);
}

void cleanupVideoDecoder(VideoDecoder& decoder)
{
    stopAsyncDecoding(decoder);
    if (decoder.packet)
    {
        av_packet_free(&decoder.packet);
    }
    if (decoder.frame)
    {
        av_frame_free(&decoder.frame);
    }
    if (decoder.nv12Frame)
    {
        av_frame_free(&decoder.nv12Frame);
    }
    if (decoder.swsCtx)
    {
        sws_freeContext(decoder.swsCtx);
        decoder.swsCtx = nullptr;
    }
    if (decoder.codecCtx)
    {
        avcodec_free_context(&decoder.codecCtx);
    }
    if (decoder.formatCtx)
    {
        avformat_close_input(&decoder.formatCtx);
    }
}

namespace
{
video::VideoColorSpace mapColorSpace(const VideoDecoder& decoder)
{
    switch (decoder.colorSpace)
    {
    case AVCOL_SPC_BT709:
        return video::VideoColorSpace::BT709;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return video::VideoColorSpace::BT2020;
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_FCC:
        return video::VideoColorSpace::BT601;
    default:
        return decoder.height >= 720 ? video::VideoColorSpace::BT709 : video::VideoColorSpace::BT601;
    }
}

video::VideoColorRange mapColorRange(const VideoDecoder& decoder)
{
    if (decoder.colorRange == AVCOL_RANGE_JPEG)
    {
        return video::VideoColorRange::Full;
    }
    return video::VideoColorRange::Limited;
}
} // namespace

VideoColorInfo deriveVideoColorInfo(const VideoDecoder& decoder)
{
    VideoColorInfo info{};
    info.colorSpace = mapColorSpace(decoder);
    info.colorRange = mapColorRange(decoder);
    return info;
}

namespace
{
struct YuvCoefficients
{
    float yR;
    float yG;
    float yB;
    float uR;
    float uG;
    float uB;
    float vR;
    float vG;
    float vB;
};

YuvCoefficients getCoefficients(VideoColorSpace space)
{
    switch (space)
    {
    case VideoColorSpace::BT601:
        return {0.299f, 0.587f, 0.114f,
                -0.168736f, -0.331264f, 0.5f,
                0.5f, -0.418688f, -0.081312f};
    case VideoColorSpace::BT2020:
        return {0.2627f, 0.6780f, 0.0593f,
                -0.13963f, -0.36037f, 0.5f,
                0.5f, -0.459786f, -0.040214f};
    case VideoColorSpace::BT709:
    default:
        return {0.2126f, 0.7152f, 0.0722f,
                -0.114572f, -0.385428f, 0.5f,
                0.5f, -0.454153f, -0.045847f};
    }
}

inline uint8_t convertLumaByte(float value, VideoColorRange range)
{
    value = std::clamp(value, 0.0f, 1.0f);
    if (range == VideoColorRange::Full)
    {
        return static_cast<uint8_t>(std::clamp(value * 255.0f, 0.0f, 255.0f));
    }
    return static_cast<uint8_t>(std::clamp(value * 219.0f + 16.0f, 0.0f, 255.0f));
}

inline uint8_t convertChromaByte(float value, VideoColorRange range)
{
    if (range == VideoColorRange::Full)
    {
        return static_cast<uint8_t>(std::clamp((value + 0.5f) * 255.0f, 0.0f, 255.0f));
    }
    return static_cast<uint8_t>(std::clamp(value * 224.0f + 128.0f, 0.0f, 255.0f));
}
} // namespace

Nv12Overlay convertOverlayToNv12(const glyph::OverlayBitmap& bitmap, const VideoColorInfo& colorInfo)
{
    Nv12Overlay overlay;
    overlay.width = bitmap.width;
    overlay.height = bitmap.height;
    overlay.offsetX = bitmap.offsetX;
    overlay.offsetY = bitmap.offsetY;

    if (overlay.width == 0 || overlay.height == 0 || bitmap.pixels.empty())
    {
        return overlay;
    }

    overlay.uvWidth = (overlay.width + 1) / 2;
    overlay.uvHeight = (overlay.height + 1) / 2;
    const size_t overlayPixelCount = static_cast<size_t>(overlay.width) * overlay.height;
    overlay.yPlane.resize(overlayPixelCount, 0);
    overlay.yAlpha.resize(overlayPixelCount, 0);
    overlay.uvPlane.resize(static_cast<size_t>(overlay.uvWidth) * overlay.uvHeight * 2, 128);
    overlay.uvAlpha.resize(static_cast<size_t>(overlay.uvWidth) * overlay.uvHeight, 0);

    const YuvCoefficients coeffs = getCoefficients(colorInfo.colorSpace);
    std::vector<float> uvAccumU(overlay.uvWidth * overlay.uvHeight, 0.0f);
    std::vector<float> uvAccumV(overlay.uvWidth * overlay.uvHeight, 0.0f);
    std::vector<float> uvAccumAlpha(overlay.uvWidth * overlay.uvHeight, 0.0f);

    for (uint32_t y = 0; y < overlay.height; ++y)
    {
        for (uint32_t x = 0; x < overlay.width; ++x)
        {
            const size_t pixelIndex = static_cast<size_t>(y) * overlay.width + x;
            const size_t rgbaIndex = pixelIndex * 4;
            const uint8_t alphaByte = bitmap.pixels[rgbaIndex + 3];
            overlay.yAlpha[pixelIndex] = alphaByte;
            if (alphaByte == 0)
            {
                continue;
            }

            const float alpha = static_cast<float>(alphaByte) / 255.0f;
            const float r = static_cast<float>(bitmap.pixels[rgbaIndex + 0]) / 255.0f;
            const float g = static_cast<float>(bitmap.pixels[rgbaIndex + 1]) / 255.0f;
            const float b = static_cast<float>(bitmap.pixels[rgbaIndex + 2]) / 255.0f;

            const float yComponent = coeffs.yR * r + coeffs.yG * g + coeffs.yB * b;
            const float uComponent = coeffs.uR * r + coeffs.uG * g + coeffs.uB * b;
            const float vComponent = coeffs.vR * r + coeffs.vG * g + coeffs.vB * b;

            overlay.yPlane[pixelIndex] = convertLumaByte(yComponent, colorInfo.colorRange);

            const uint8_t uByte = convertChromaByte(uComponent, colorInfo.colorRange);
            const uint8_t vByte = convertChromaByte(vComponent, colorInfo.colorRange);

            const uint32_t blockX = x / 2;
            const uint32_t blockY = y / 2;
            const size_t blockIndex = static_cast<size_t>(blockY) * overlay.uvWidth + blockX;
            uvAccumU[blockIndex] += alpha * static_cast<float>(uByte);
            uvAccumV[blockIndex] += alpha * static_cast<float>(vByte);
            uvAccumAlpha[blockIndex] += alpha;
        }
    }

    for (size_t blockIndex = 0; blockIndex < uvAccumAlpha.size(); ++blockIndex)
    {
        const float accumulatedAlpha = std::min(1.0f, uvAccumAlpha[blockIndex]);
        overlay.uvAlpha[blockIndex] = static_cast<uint8_t>(std::clamp(accumulatedAlpha * 255.0f, 0.0f, 255.0f));
        if (uvAccumAlpha[blockIndex] > 0.0f)
        {
            const float invAlpha = 1.0f / uvAccumAlpha[blockIndex];
            const uint8_t uByte = static_cast<uint8_t>(std::clamp(uvAccumU[blockIndex] * invAlpha, 0.0f, 255.0f));
            const uint8_t vByte = static_cast<uint8_t>(std::clamp(uvAccumV[blockIndex] * invAlpha, 0.0f, 255.0f));
            overlay.uvPlane[blockIndex * 2] = uByte;
            overlay.uvPlane[blockIndex * 2 + 1] = vByte;
        }
        else
        {
            overlay.uvPlane[blockIndex * 2] = 128;
            overlay.uvPlane[blockIndex * 2 + 1] = 128;
        }
    }

    return overlay;
}

void applyNv12Overlay(std::vector<uint8_t>& nv12Buffer,
                      uint32_t frameWidth,
                      uint32_t frameHeight,
                      const Nv12Overlay& overlay)
{
    if (!overlay.isValid() || nv12Buffer.empty() || frameWidth == 0 || frameHeight == 0)
    {
        return;
    }

    const size_t lumaSize = static_cast<size_t>(frameWidth) * frameHeight;
    if (nv12Buffer.size() < lumaSize + lumaSize / 2)
    {
        return;
    }

    uint8_t* yPlane = nv12Buffer.data();
    uint8_t* uvPlane = nv12Buffer.data() + lumaSize;

    for (uint32_t y = 0; y < overlay.height; ++y)
    {
        const uint32_t frameY = overlay.offsetY + y;
        if (frameY >= frameHeight)
        {
            break;
        }

        for (uint32_t x = 0; x < overlay.width; ++x)
        {
            const uint32_t frameX = overlay.offsetX + x;
            if (frameX >= frameWidth)
            {
                break;
            }

            const size_t overlayIndex = static_cast<size_t>(y) * overlay.width + x;
            const uint8_t alphaByte = overlay.yAlpha[overlayIndex];
            if (alphaByte == 0)
            {
                continue;
            }

            const float alpha = static_cast<float>(alphaByte) / 255.0f;
            const size_t dstIndex = static_cast<size_t>(frameY) * frameWidth + frameX;
            const uint8_t overlayY = overlay.yPlane[overlayIndex];
            const uint8_t baseY = yPlane[dstIndex];
            yPlane[dstIndex] = static_cast<uint8_t>(alpha * overlayY + (1.0f - alpha) * baseY + 0.5f);
        }
    }

    const uint32_t frameUvWidth = frameWidth / 2;
    const uint32_t frameUvHeight = frameHeight / 2;
    if (frameUvWidth == 0 || frameUvHeight == 0)
    {
        return;
    }

    for (uint32_t by = 0; by < overlay.uvHeight; ++by)
    {
        const uint32_t frameBlockY = (overlay.offsetY / 2) + by;
        if (frameBlockY >= frameUvHeight)
        {
            break;
        }

        for (uint32_t bx = 0; bx < overlay.uvWidth; ++bx)
        {
            const uint32_t frameBlockX = (overlay.offsetX / 2) + bx;
            if (frameBlockX >= frameUvWidth)
            {
                break;
            }

            const size_t blockIndex = static_cast<size_t>(by) * overlay.uvWidth + bx;
            const uint8_t alphaByte = overlay.uvAlpha[blockIndex];
            if (alphaByte == 0)
            {
                continue;
            }

            const float alpha = static_cast<float>(alphaByte) / 255.0f;
            const size_t dstIndex = static_cast<size_t>(frameBlockY) * frameWidth + frameBlockX * 2;
            const uint8_t overlayU = overlay.uvPlane[blockIndex * 2];
            const uint8_t overlayV = overlay.uvPlane[blockIndex * 2 + 1];
            const uint8_t baseU = uvPlane[dstIndex];
            const uint8_t baseV = uvPlane[dstIndex + 1];
            uvPlane[dstIndex] = static_cast<uint8_t>(alpha * overlayU + (1.0f - alpha) * baseU + 0.5f);
            uvPlane[dstIndex + 1] = static_cast<uint8_t>(alpha * overlayV + (1.0f - alpha) * baseV + 0.5f);
        }
    }
}

std::vector<Vertex> buildVideoQuadVertices(float width, float height)
{
    const float halfWidth = width * 0.5f;
    const float halfHeight = height * 0.5f;
    return {
        {{-halfWidth, -halfHeight, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{halfWidth, -halfHeight, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{halfWidth, halfHeight, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-halfWidth, -halfHeight, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
        {{halfWidth, halfHeight, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-halfWidth, halfHeight, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}};
}

} // namespace video

// Global function for video playback mode
int runVideoPlayback(bool msaaOverride, VkSampleCountFlagBits requestedMsaa)
{
    std::cout << "[Video] Starting video playback mode..." << std::endl;

    // Hardcoded video file path
    std::string videoPath = "../P1090533.MOV";
    
    // Check if file exists using basic file operations
    std::ifstream testFile(videoPath);
    if (!testFile.good())
    {
        std::cerr << "[Video] Could not find video file: " << videoPath << std::endl;
        return 1;
    }
    testFile.close();

    std::cout << "[Video] Using video file: " << videoPath << std::endl;

    // Initialize video decoder
    video::VideoDecoder decoder;
    if (!video::initializeVideoDecoder(videoPath, decoder))
    {
        std::cerr << "[Video] Failed to initialize video decoder" << std::endl;
        return 1;
    }

    const auto colorInfo = deriveVideoColorInfo(decoder);

    std::cout << "[Video] Video dimensions: " << decoder.width << "x" << decoder.height 
              << ", FPS: " << decoder.fps << std::endl;

    // Create engine instance
    Engine *engine = new Engine();
    if (msaaOverride)
    {
        engine->setMsaaSampleCount(requestedMsaa);
        std::cout << "[Info] Requested MSAA " << msaaIntFromFlag(requestedMsaa)
                  << "x. Using " << msaaIntFromFlag(engine->getMsaaSampleCount()) << "x.\n";
    }

    // Create display window
    Display* display = engine->createWindow(800, 600, "Motive Video Player");

    // Set up lighting
    Light sceneLight(glm::vec3(0.0f, 0.0f, 1.0f),
                     glm::vec3(0.1f),
                     glm::vec3(0.9f));
    sceneLight.setDiffuse(glm::vec3(1.0f, 0.95f, 0.9f));
    engine->setLight(sceneLight);

    // Create camera
    glm::vec3 defaultCameraPos(0.0f, 0.0f, 3.0f);
    glm::vec2 defaultCameraRotation(glm::radians(0.0f), 0.0f);
    auto* primaryCamera = new Camera(engine, display, defaultCameraPos, defaultCameraRotation);
    display->addCamera(primaryCamera);

    // Create video quad geometry
    float quadWidth = 1.6f;  // 16:9 aspect ratio
    float quadHeight = 0.9f;
    auto videoVertices = video::buildVideoQuadVertices(quadWidth, quadHeight);
    
    // Create model with video quad
    auto videoModel = std::make_unique<Model>(videoVertices, engine);
    engine->addModel(std::move(videoModel));

    // Get the primitive that will display the video
    if (engine->models.empty() || engine->models[0]->meshes.empty() || 
        engine->models[0]->meshes[0].primitives.empty())
    {
        std::cerr << "[Video] Failed to create video quad geometry" << std::endl;
        cleanupVideoDecoder(decoder);
        delete engine;
        return 1;
    }

    auto& primitive = engine->models[0]->meshes[0].primitives[0];
    primitive->setYuvColorMetadata(static_cast<uint32_t>(colorInfo.colorSpace),
                                   static_cast<uint32_t>(colorInfo.colorRange));
    primitive->enableTextureDoubleBuffering();
    
    // Create initial texture for video using gray NV12 data
    std::vector<uint8_t> initialFrame(static_cast<size_t>(decoder.bufferSize), 128);
    primitive->updateTextureFromNV12(
        initialFrame.data(),
        initialFrame.size(),
        decoder.width,
        decoder.height
    );

    if (!video::startAsyncDecoding(decoder, 12))
    {
        std::cerr << "[Video] Failed to start async decoder" << std::endl;
        cleanupVideoDecoder(decoder);
        delete engine;
        return 1;
    }

    std::cout << "[Video] Starting video playback loop..." << std::endl;

    video::Nv12Overlay fpsOverlay = video::convertOverlayToNv12(
        glyph::buildLabeledOverlay(static_cast<uint32_t>(decoder.width),
                                   static_cast<uint32_t>(decoder.height),
                                   "VID",
                                   0.0f),
        colorInfo);
    bool overlayValid = fpsOverlay.isValid();
    size_t overlayFrameCounter = 0;
    auto overlayTimer = std::chrono::steady_clock::now();

    // Video playback loop
    std::vector<uint8_t> frameBuffer;
    auto frameDuration = std::chrono::duration<double>(1.0 / decoder.fps);
    auto lastFrameTime = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(display->window))
    {
        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = currentTime - lastFrameTime;

        // Decode and update frame if enough time has passed
        if (elapsed >= frameDuration)
        {
            if (video::acquireDecodedFrame(decoder, frameBuffer))
            {
                overlayFrameCounter++;
                auto overlayNow = std::chrono::steady_clock::now();
                auto overlayElapsed = std::chrono::duration<double>(overlayNow - overlayTimer);
                if (overlayElapsed.count() >= 0.25)
                {
                    const float fpsValue = overlayElapsed.count() > 0.0 ?
                        static_cast<float>(overlayFrameCounter / overlayElapsed.count()) : 0.0f;
                    fpsOverlay = video::convertOverlayToNv12(
                        glyph::buildLabeledOverlay(static_cast<uint32_t>(decoder.width),
                                                    static_cast<uint32_t>(decoder.height),
                                                    "VID",
                                                    fpsValue),
                        colorInfo);
                    overlayValid = fpsOverlay.isValid();
                    overlayFrameCounter = 0;
                    overlayTimer = overlayNow;
                }

                if (overlayValid)
                {
                    video::applyNv12Overlay(frameBuffer, decoder.width, decoder.height, fpsOverlay);
                }

                primitive->updateTextureFromNV12(
                    frameBuffer.data(),
                    frameBuffer.size(),
                    decoder.width,
                    decoder.height
                );
                lastFrameTime = currentTime;
            }
            else if (decoder.finished.load() && !decoder.threadRunning.load())
            {
                std::cout << "[Video] End of video reached" << std::endl;
                break;
            }
        }

        // Render frame
        display->render();

        // Handle events
        glfwPollEvents();
    }

    std::cout << "[Video] Video playback completed" << std::endl;

    // Cleanup
    cleanupVideoDecoder(decoder);
    delete engine;

    return 0;
}
