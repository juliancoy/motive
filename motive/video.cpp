#include "video.h"
#include "engine.h"
#include "display.h"
#include "camera.h"
#include "model.h"
#include "light.h"
#include "utils.h"
#include "glyph.h"
#include "video_frame_utils.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>
#include <limits>
#include <system_error>
#include <cstring>
#include <sstream>
#include <deque>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

namespace {

using video::DecodeImplementation;
using video::VideoDecoder;

std::string ffmpegErrorString(int errnum)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, buffer, sizeof(buffer));
    return std::string(buffer);
}

const char* hwDeviceTypeName(AVHWDeviceType type)
{
    switch (type)
    {
    case AV_HWDEVICE_TYPE_VAAPI:
        return "VAAPI";
    case AV_HWDEVICE_TYPE_DXVA2:
        return "DXVA2";
    case AV_HWDEVICE_TYPE_D3D11VA:
        return "D3D11";
    case AV_HWDEVICE_TYPE_CUDA:
        return "CUDA";
    case AV_HWDEVICE_TYPE_VDPAU:
        return "VDPAU";
    case AV_HWDEVICE_TYPE_VIDEOTOOLBOX:
        return "VideoToolbox";
    case AV_HWDEVICE_TYPE_QSV:
        return "QuickSync";
    case AV_HWDEVICE_TYPE_MEDIACODEC:
        return "MediaCodec";
    case AV_HWDEVICE_TYPE_VULKAN:
        return "Vulkan";
    default:
        return "Unknown";
    }
}

const char* pixelFormatName(AVPixelFormat fmt)
{
    const char* name = av_get_pix_fmt_name(fmt);
    return name ? name : "unknown";
}

std::string pixelFormatDescription(AVPixelFormat fmt)
{
    std::ostringstream oss;
    oss << pixelFormatName(fmt) << " (" << static_cast<int>(fmt) << ")";
    return oss.str();
}

int determineStreamBitDepth(const AVStream* videoStream, const AVCodecContext* codecCtx)
{
    if (videoStream && videoStream->codecpar && videoStream->codecpar->bits_per_raw_sample > 0)
    {
        return videoStream->codecpar->bits_per_raw_sample;
    }

    if (codecCtx && codecCtx->bits_per_raw_sample > 0)
    {
        return codecCtx->bits_per_raw_sample;
    }

    if (videoStream && videoStream->codecpar)
    {
        const AVPixFmtDescriptor* desc =
            av_pix_fmt_desc_get(static_cast<AVPixelFormat>(videoStream->codecpar->format));
        if (desc && desc->comp[0].depth > 0)
        {
            return desc->comp[0].depth;
        }
    }

    return 8;
}

AVPixelFormat pickPreferredSwPixelFormat(int bitDepth)
{
#if defined(AV_PIX_FMT_P010)
    if (bitDepth > 8)
    {
        return AV_PIX_FMT_P010;
    }
#endif
    return AV_PIX_FMT_NV12;
}

AVPixelFormat getHardwareFormat(AVCodecContext* ctx, const AVPixelFormat* pixFmts)
{
    auto* decoder = reinterpret_cast<VideoDecoder*>(ctx->opaque);
    if (!decoder)
    {
        return pixFmts[0];
    }

    for (const AVPixelFormat* fmt = pixFmts; *fmt != AV_PIX_FMT_NONE; ++fmt)
    {
        if (*fmt == decoder->hwPixelFormat)
        {
            return *fmt;
        }
    }

    std::cerr << "[Video] Requested hardware pixel format not supported by FFmpeg." << std::endl;
    return pixFmts[0];
}

bool codecSupportsDevice(const AVCodec* codec, AVHWDeviceType type, AVPixelFormat& outFormat)
{
    for (int i = 0;; ++i)
    {
        const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
        if (!config)
        {
            break;
        }

        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
            config->device_type == type)
        {
            outFormat = config->pix_fmt;
            return true;
        }
    }
    return false;
}

bool trySetupHardwareDecoder(VideoDecoder& decoder,
                             const AVCodec* codec,
                             AVHWDeviceType type,
                             DecodeImplementation implementation)
{
    AVPixelFormat hwFormat = AV_PIX_FMT_NONE;
    if (!codecSupportsDevice(codec, type, hwFormat))
    {
        return false;
    }

    AVBufferRef* hwDeviceCtx = nullptr;
    int err = av_hwdevice_ctx_create(&hwDeviceCtx, type, nullptr, nullptr, 0);
    if (err < 0)
    {
        std::cerr << "[Video] Failed to create " << hwDeviceTypeName(type)
                  << " hardware context: " << ffmpegErrorString(err) << std::endl;
        return false;
    }

    decoder.hwDeviceCtx = hwDeviceCtx;
    decoder.hwPixelFormat = hwFormat;
    decoder.hwDeviceType = type;
    decoder.codecCtx->opaque = &decoder;
    decoder.codecCtx->get_format = getHardwareFormat;
    decoder.codecCtx->hw_device_ctx = av_buffer_ref(decoder.hwDeviceCtx);
    if (!decoder.codecCtx->hw_device_ctx)
    {
        std::cerr << "[Video] Failed to reference hardware context." << std::endl;
        av_buffer_unref(&decoder.hwDeviceCtx);
        decoder.hwDeviceCtx = nullptr;
        return false;
    }

    decoder.implementation = implementation;
    std::string label = implementation == DecodeImplementation::Vulkan
                            ? "Vulkan"
                            : hwDeviceTypeName(type);
    decoder.implementationName = label + " hardware";
    std::cout << "[Video] " << label << " decoder reports hardware pixel format "
              << pixelFormatDescription(hwFormat) << std::endl;
    return true;
}

bool configureDecodeImplementation(VideoDecoder& decoder,
                                   const AVCodec* codec,
                                   DecodeImplementation implementation)
{
    decoder.implementation = DecodeImplementation::Software;
    decoder.implementationName = "Software (CPU)";
    decoder.hwDeviceType = AV_HWDEVICE_TYPE_NONE;
    decoder.hwPixelFormat = AV_PIX_FMT_NONE;

    if (implementation == DecodeImplementation::Software)
    {
        return true;
    }

    if (implementation == DecodeImplementation::Vulkan)
    {
        if (trySetupHardwareDecoder(decoder, codec, AV_HWDEVICE_TYPE_VULKAN, implementation))
        {
            return true;
        }
        std::cerr << "[Video] Vulkan decode not available for this codec/hardware combination."
                  << std::endl;
        return false;
    }

    std::cerr << "[Video] Unsupported decode implementation requested." << std::endl;
    return false;
}

} // namespace

namespace video {

bool initializeVideoDecoder(const std::filesystem::path& videoPath,
                            VideoDecoder& decoder,
                            const DecoderInitParams& initParams)
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

    if (!configureDecodeImplementation(decoder, codec, initParams.implementation))
    {
        return false;
    }

    const int streamBitDepth = determineStreamBitDepth(videoStream, decoder.codecCtx);
    if (decoder.implementation != DecodeImplementation::Software)
    {
        decoder.requestedSwPixelFormat = pickPreferredSwPixelFormat(streamBitDepth);
        decoder.codecCtx->sw_pix_fmt = decoder.requestedSwPixelFormat;
        std::cout << "[Video] Requesting " << pixelFormatDescription(decoder.requestedSwPixelFormat)
                  << " software frames from " << decoder.implementationName
                  << " decoder (bit depth " << streamBitDepth << ")" << std::endl;
    }
    else
    {
        decoder.requestedSwPixelFormat = decoder.codecCtx->pix_fmt;
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
    if (decoder.implementation != DecodeImplementation::Software)
    {
        decoder.swFrame = av_frame_alloc();
    }
    decoder.packet = av_packet_alloc();
    if (!decoder.frame || !decoder.packet ||
        (decoder.implementation != DecodeImplementation::Software && !decoder.swFrame))
    {
        std::cerr << "[Video] Failed to allocate FFmpeg structures." << std::endl;
        return false;
    }

    decoder.streamTimeBase = videoStream->time_base;
    decoder.fallbackPtsSeconds = 0.0;
    decoder.framesDecoded = 0;
    decoder.colorSpace = decoder.codecCtx->colorspace;
    decoder.colorRange = decoder.codecCtx->color_range;
    decoder.planarYuv = false;
    decoder.chromaInterleaved = false;
    decoder.outputFormat = PrimitiveYuvFormat::NV12;
    decoder.bytesPerComponent = 1;
    decoder.bitDepth = 8;
    decoder.chromaDivX = 2;
    decoder.chromaDivY = 2;
    decoder.chromaWidth = std::max<uint32_t>(1u, static_cast<uint32_t>((decoder.width + decoder.chromaDivX - 1) / decoder.chromaDivX));
    decoder.chromaHeight = std::max<uint32_t>(1u, static_cast<uint32_t>((decoder.height + decoder.chromaDivY - 1) / decoder.chromaDivY));
    decoder.yPlaneBytes = 0;
    decoder.uvPlaneBytes = 0;

    if (!configureFormatForPixelFormat(decoder, decoder.codecCtx->pix_fmt))
    {
        std::cerr << "[Video] Unsupported pixel format for decoder: "
                  << pixelFormatDescription(decoder.codecCtx->pix_fmt) << std::endl;
        return false;
    }

    AVRational frameRate = av_guess_frame_rate(decoder.formatCtx, videoStream, nullptr);
    double fps = (frameRate.num != 0 && frameRate.den != 0) ? av_q2d(frameRate) : 30.0;
    decoder.fps = fps > 0.0 ? fps : 30.0;
    std::cout << "[Video] Using " << decoder.implementationName;
    if (decoder.implementation != DecodeImplementation::Software)
    {
        std::cout << " (hw " << pixelFormatName(decoder.hwPixelFormat);
        if (decoder.requestedSwPixelFormat != AV_PIX_FMT_NONE)
        {
            std::cout << " -> sw " << pixelFormatName(decoder.requestedSwPixelFormat);
        }
        std::cout << ")";
    }
    std::cout << " decoder" << std::endl;
    return true;
}

bool decodeNextFrame(VideoDecoder& decoder, DecodedFrame& decodedFrame)
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
            AVFrame* workingFrame = decoder.frame;
            if (decoder.implementation != DecodeImplementation::Software &&
                decoder.frame->format == decoder.hwPixelFormat)
            {
                if (!decoder.swFrame)
                {
                    decoder.swFrame = av_frame_alloc();
                    if (!decoder.swFrame)
                    {
                        std::cerr << "[Video] Failed to allocate sw transfer frame." << std::endl;
                        return false;
                    }
                }
                av_frame_unref(decoder.swFrame);
                int transferResult = av_hwframe_transfer_data(decoder.swFrame, decoder.frame, 0);
                if (transferResult < 0)
                {
                    std::cerr << "[Video] Failed to transfer hardware frame: "
                              << ffmpegErrorString(transferResult) << std::endl;
                    return false;
                }
                workingFrame = decoder.swFrame;
            }

            const AVPixelFormat frameFormat = static_cast<AVPixelFormat>(workingFrame->format);
            if (decoder.width != workingFrame->width || decoder.height != workingFrame->height ||
                frameFormat != decoder.sourcePixelFormat)
            {
                decoder.width = workingFrame->width;
                decoder.height = workingFrame->height;
                if (!configureFormatForPixelFormat(decoder, frameFormat))
                {
                    std::cerr << "[Video] Unsupported pixel format during decode: "
                              << pixelFormatDescription(frameFormat) << std::endl;
                    return false;
                }

                std::cout << "[Video] Decoder output pixel format changed to "
                          << pixelFormatDescription(frameFormat) << std::endl;
            }

            copyDecodedFrameToBuffer(decoder, workingFrame, decodedFrame.buffer);

            double ptsSeconds = decoder.fallbackPtsSeconds;
            const int64_t bestTimestamp = workingFrame->best_effort_timestamp;
            if (bestTimestamp != AV_NOPTS_VALUE)
            {
                const double timeBase = decoder.streamTimeBase.den != 0
                                            ? static_cast<double>(decoder.streamTimeBase.num) /
                                                  static_cast<double>(decoder.streamTimeBase.den)
                                            : 0.0;
                ptsSeconds = timeBase * static_cast<double>(bestTimestamp);
            }
            else
            {
                const double frameDuration = decoder.fps > 0.0 ? (1.0 / decoder.fps) : (1.0 / 30.0);
                ptsSeconds = decoder.framesDecoded > 0
                                 ? decoder.fallbackPtsSeconds + frameDuration
                                 : 0.0;
            }
            decoder.fallbackPtsSeconds = ptsSeconds;
            decoder.framesDecoded++;
            decodedFrame.ptsSeconds = ptsSeconds;

            av_frame_unref(decoder.frame);
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
    video::DecodedFrame localFrame;
    localFrame.buffer.reserve(decoder->bufferSize);
    while (!decoder->stopRequested.load())
    {
        if (!video::decodeNextFrame(*decoder, localFrame))
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

        decoder->frameQueue.emplace_back(std::move(localFrame));
        lock.unlock();
        decoder->frameCond.notify_all();
        localFrame = video::DecodedFrame{};
        localFrame.buffer.reserve(decoder->bufferSize);
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

bool acquireDecodedFrame(VideoDecoder& decoder, DecodedFrame& outFrame)
{
    std::unique_lock<std::mutex> lock(decoder.frameMutex);
    if (decoder.frameQueue.empty())
    {
        return false;
    }

    outFrame = std::move(decoder.frameQueue.front());
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
    if (decoder.swFrame)
    {
        av_frame_free(&decoder.swFrame);
    }
    if (decoder.hwFramesCtx)
    {
        av_buffer_unref(&decoder.hwFramesCtx);
        decoder.hwFramesCtx = nullptr;
    }
    if (decoder.hwDeviceCtx)
    {
        av_buffer_unref(&decoder.hwDeviceCtx);
        decoder.hwDeviceCtx = nullptr;
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

namespace {

uint32_t computeBitDepthScale(uint32_t bitDepth)
{
    if (bitDepth <= 8)
    {
        return 1u;
    }
    const uint32_t shift = bitDepth - 8;
    if (shift >= 24)
    {
        return 1u << 24;
    }
    return 1u << shift;
}

uint8_t blendComponent8(uint8_t base, uint8_t overlay, float alpha)
{
    const float blended = alpha * static_cast<float>(overlay) +
                          (1.0f - alpha) * static_cast<float>(base);
    return static_cast<uint8_t>(std::clamp(blended + 0.5f, 0.0f, 255.0f));
}

uint16_t blendComponent16(uint16_t base,
                          uint16_t overlay,
                          float alpha,
                          uint16_t maxCode)
{
    const float blended = alpha * static_cast<float>(overlay) +
                          (1.0f - alpha) * static_cast<float>(base);
    return static_cast<uint16_t>(std::clamp(blended + 0.5f,
                                             0.0f,
                                             static_cast<float>(maxCode)));
}

void applyPlanarOverlay8(std::vector<uint8_t>& buffer,
                         const VideoDecoder& decoder,
                         const Nv12Overlay& overlay)
{
    if (decoder.width == 0 || decoder.height == 0)
    {
        return;
    }

    uint8_t* yPlane = buffer.data();
    for (uint32_t y = 0; y < overlay.height; ++y)
    {
        const uint32_t frameY = overlay.offsetY + y;
        if (frameY >= static_cast<uint32_t>(decoder.height))
        {
            break;
        }

        uint8_t* dstRow = yPlane + static_cast<size_t>(frameY) * decoder.width;
        for (uint32_t x = 0; x < overlay.width; ++x)
        {
            const uint32_t frameX = overlay.offsetX + x;
            if (frameX >= static_cast<uint32_t>(decoder.width))
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
            const uint8_t overlayValue = overlay.yPlane[overlayIndex];
            uint8_t& dstValue = dstRow[frameX];
            dstValue = blendComponent8(dstValue, overlayValue, alpha);
        }
    }

    if (decoder.chromaWidth == 0 || decoder.chromaHeight == 0)
    {
        return;
    }

    uint8_t* uvPlane = buffer.data() + decoder.yPlaneBytes;
    const size_t uvStride = static_cast<size_t>(decoder.chromaWidth) * 2u;
    const uint32_t chromaOffsetX = decoder.chromaDivX > 0 ? overlay.offsetX / decoder.chromaDivX : overlay.offsetX;
    const uint32_t chromaOffsetY = decoder.chromaDivY > 0 ? overlay.offsetY / decoder.chromaDivY : overlay.offsetY;

    for (uint32_t by = 0; by < overlay.uvHeight; ++by)
    {
        const uint32_t frameBlockY = chromaOffsetY + by;
        if (frameBlockY >= decoder.chromaHeight)
        {
            break;
        }

        uint8_t* dstRow = uvPlane + static_cast<size_t>(frameBlockY) * uvStride;
        for (uint32_t bx = 0; bx < overlay.uvWidth; ++bx)
        {
            const uint32_t frameBlockX = chromaOffsetX + bx;
            if (frameBlockX >= decoder.chromaWidth)
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
            uint8_t* dst = dstRow + static_cast<size_t>(frameBlockX) * 2u;
            const uint8_t overlayU = overlay.uvPlane[blockIndex * 2u];
            const uint8_t overlayV = overlay.uvPlane[blockIndex * 2u + 1u];
            dst[0] = blendComponent8(dst[0], overlayU, alpha);
            dst[1] = blendComponent8(dst[1], overlayV, alpha);
        }
    }
}

void applyPlanarOverlay16(std::vector<uint8_t>& buffer,
                          const VideoDecoder& decoder,
                          const Nv12Overlay& overlay)
{
    if (decoder.width == 0 || decoder.height == 0)
    {
        return;
    }

    uint16_t* yPlane = reinterpret_cast<uint16_t*>(buffer.data());
    const uint32_t maxCode = decoder.bitDepth > 0 ? ((1u << decoder.bitDepth) - 1u) : 0xffffu;
    const uint32_t scaleFactor = computeBitDepthScale(decoder.bitDepth);

    for (uint32_t y = 0; y < overlay.height; ++y)
    {
        const uint32_t frameY = overlay.offsetY + y;
        if (frameY >= static_cast<uint32_t>(decoder.height))
        {
            break;
        }

        uint16_t* dstRow = yPlane + static_cast<size_t>(frameY) * decoder.width;
        for (uint32_t x = 0; x < overlay.width; ++x)
        {
            const uint32_t frameX = overlay.offsetX + x;
            if (frameX >= static_cast<uint32_t>(decoder.width))
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
            const uint32_t overlayValue = std::min<uint32_t>(
                static_cast<uint32_t>(overlay.yPlane[overlayIndex]) * scaleFactor,
                maxCode);
            uint16_t& dstValue = dstRow[frameX];
            dstValue = blendComponent16(dstValue,
                                        static_cast<uint16_t>(overlayValue),
                                        alpha,
                                        static_cast<uint16_t>(maxCode));
        }
    }

    if (decoder.chromaWidth == 0 || decoder.chromaHeight == 0)
    {
        return;
    }

    uint16_t* uvPlane = reinterpret_cast<uint16_t*>(buffer.data() + decoder.yPlaneBytes);
    const size_t uvStride = static_cast<size_t>(decoder.chromaWidth) * 2u;
    const uint32_t chromaOffsetX = decoder.chromaDivX > 0 ? overlay.offsetX / decoder.chromaDivX : overlay.offsetX;
    const uint32_t chromaOffsetY = decoder.chromaDivY > 0 ? overlay.offsetY / decoder.chromaDivY : overlay.offsetY;

    for (uint32_t by = 0; by < overlay.uvHeight; ++by)
    {
        const uint32_t frameBlockY = chromaOffsetY + by;
        if (frameBlockY >= decoder.chromaHeight)
        {
            break;
        }

        uint16_t* dstRow = uvPlane + static_cast<size_t>(frameBlockY) * uvStride;
        for (uint32_t bx = 0; bx < overlay.uvWidth; ++bx)
        {
            const uint32_t frameBlockX = chromaOffsetX + bx;
            if (frameBlockX >= decoder.chromaWidth)
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
            uint16_t* dst = dstRow + static_cast<size_t>(frameBlockX) * 2u;
            const uint32_t overlayU = std::min<uint32_t>(
                static_cast<uint32_t>(overlay.uvPlane[blockIndex * 2u]) * scaleFactor,
                maxCode);
            const uint32_t overlayV = std::min<uint32_t>(
                static_cast<uint32_t>(overlay.uvPlane[blockIndex * 2u + 1u]) * scaleFactor,
                maxCode);
            dst[0] = blendComponent16(dst[0], static_cast<uint16_t>(overlayU), alpha, static_cast<uint16_t>(maxCode));
            dst[1] = blendComponent16(dst[1], static_cast<uint16_t>(overlayV), alpha, static_cast<uint16_t>(maxCode));
        }
    }
}

} // namespace

void applyOverlayToDecodedFrame(std::vector<uint8_t>& buffer,
                                const VideoDecoder& decoder,
                                const Nv12Overlay& overlay)
{
    if (!overlay.isValid() || buffer.empty())
    {
        return;
    }

    if (decoder.outputFormat == PrimitiveYuvFormat::NV12)
    {
        applyNv12Overlay(buffer,
                         static_cast<uint32_t>(decoder.width),
                         static_cast<uint32_t>(decoder.height),
                         overlay);
        return;
    }

    if (!decoder.planarYuv)
    {
        return;
    }

    if (decoder.bytesPerComponent <= 1)
    {
        applyPlanarOverlay8(buffer, decoder, overlay);
    }
    else
    {
        applyPlanarOverlay16(buffer, decoder, overlay);
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

std::vector<std::string> listAvailableHardwareDevices()
{
    std::vector<std::string> devices;
    AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
    while (true)
    {
        type = av_hwdevice_iterate_types(type);
        if (type == AV_HWDEVICE_TYPE_NONE)
        {
            break;
        }
        const char* name = av_hwdevice_get_type_name(type);
        if (name && *name)
        {
            devices.emplace_back(name);
        }
    }
    return devices;
}

} // namespace video

// Global function for video playback mode
int runVideoPlayback(bool msaaOverride, VkSampleCountFlagBits requestedMsaa)
{
    std::cout << "[Video] Starting video playback mode..." << std::endl;

    // Hardcoded video file path
    std::string videoPath = "../P1090533_main8_hevc_fast.mkv";
    
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
    video::DecoderInitParams playbackParams{};
    if (!video::initializeVideoDecoder(videoPath, decoder, playbackParams))
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
    
    std::vector<uint8_t> initialFrame(static_cast<size_t>(decoder.bufferSize), 0);
    if (decoder.outputFormat == PrimitiveYuvFormat::NV12)
    {
        const size_t yBytes = decoder.yPlaneBytes;
        if (yBytes > 0 && yBytes <= initialFrame.size())
        {
            std::fill(initialFrame.begin(), initialFrame.begin() + yBytes, 0x80);
            std::fill(initialFrame.begin() + yBytes, initialFrame.end(), 0x80);
        }
        primitive->updateTextureFromNV12(
            initialFrame.data(),
            initialFrame.size(),
            decoder.width,
            decoder.height);
    }
    else
    {
        const size_t yBytes = decoder.yPlaneBytes;
        const size_t uvBytes = decoder.uvPlaneBytes;
        const bool sixteenBit = decoder.bytesPerComponent > 1;
        if (sixteenBit)
        {
            const uint32_t bitDepth = decoder.bitDepth > 0 ? decoder.bitDepth : 8;
            const uint32_t shift = bitDepth >= 16 ? 0u : 16u - bitDepth;
            const uint16_t baseValue = static_cast<uint16_t>(1u << (bitDepth > 0 ? bitDepth - 1 : 7));
            const uint16_t fillValue = static_cast<uint16_t>(baseValue << shift);
            if (yBytes >= sizeof(uint16_t))
            {
                uint16_t* yDst = reinterpret_cast<uint16_t*>(initialFrame.data());
                std::fill(yDst, yDst + (yBytes / sizeof(uint16_t)), fillValue);
            }
            if (uvBytes >= sizeof(uint16_t))
            {
                uint16_t* uvDst = reinterpret_cast<uint16_t*>(initialFrame.data() + yBytes);
                std::fill(uvDst, uvDst + (uvBytes / sizeof(uint16_t)), fillValue);
            }
        }
        else
        {
            if (yBytes > 0 && yBytes <= initialFrame.size())
            {
                std::fill(initialFrame.begin(), initialFrame.begin() + yBytes, 0x80);
            }
            if (uvBytes > 0 && yBytes + uvBytes <= initialFrame.size())
            {
                std::fill(initialFrame.begin() + yBytes, initialFrame.begin() + yBytes + uvBytes, 0x80);
            }
        }
        primitive->updateTextureFromPlanarYuv(
            initialFrame.data(),
            yBytes,
            decoder.width,
            decoder.height,
            initialFrame.data() + yBytes,
            uvBytes,
            decoder.chromaWidth,
            decoder.chromaHeight,
            sixteenBit,
            decoder.outputFormat);
    }

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
    std::deque<video::DecodedFrame> pendingFrames;
    video::DecodedFrame decodedFrame;
    bool playbackClockInitialized = false;
    auto playbackStartTime = std::chrono::steady_clock::now();
    double basePtsSeconds = 0.0;

    while (!glfwWindowShouldClose(display->window))
    {
        constexpr size_t kMaxPendingFrames = 3;
        while (pendingFrames.size() < kMaxPendingFrames &&
               video::acquireDecodedFrame(decoder, decodedFrame))
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

            pendingFrames.emplace_back(std::move(decodedFrame));
        }

        const auto currentTime = std::chrono::steady_clock::now();
        if (!pendingFrames.empty())
        {
            auto& nextFrame = pendingFrames.front();
            if (!playbackClockInitialized)
            {
                playbackClockInitialized = true;
                basePtsSeconds = nextFrame.ptsSeconds;
                playbackStartTime = currentTime;
            }

            const double relativePts = std::max(0.0, nextFrame.ptsSeconds - basePtsSeconds);
            const auto targetTime = playbackStartTime + std::chrono::duration<double>(relativePts);
            if (currentTime + std::chrono::milliseconds(1) >= targetTime)
            {
                video::DecodedFrame frame = std::move(nextFrame);
                pendingFrames.pop_front();

                if (overlayValid)
                {
                    video::applyOverlayToDecodedFrame(frame.buffer, decoder, fpsOverlay);
                }

                if (decoder.outputFormat == PrimitiveYuvFormat::NV12)
                {
                    primitive->updateTextureFromNV12(
                        frame.buffer.data(),
                        frame.buffer.size(),
                        decoder.width,
                        decoder.height);
                }
                else
                {
                    const uint8_t* yPlane = frame.buffer.data();
                    const uint8_t* uvPlane = yPlane + decoder.yPlaneBytes;
                    primitive->updateTextureFromPlanarYuv(
                        yPlane,
                        decoder.yPlaneBytes,
                        decoder.width,
                        decoder.height,
                        uvPlane,
                        decoder.uvPlaneBytes,
                        decoder.chromaWidth,
                        decoder.chromaHeight,
                        decoder.bytesPerComponent > 1,
                        decoder.outputFormat);
                }
            }
        }
        else if (decoder.finished.load() && !decoder.threadRunning.load())
        {
            std::cout << "[Video] End of video reached" << std::endl;
            break;
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
