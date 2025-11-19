#include "video.h"

#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace video {

std::optional<std::filesystem::path> locateVideoFile(const std::string& filename)
{
    namespace fs = std::filesystem;
    fs::path current = fs::current_path();
    for (int i = 0; i < 6; ++i)
    {
        fs::path candidate = current / filename;
        if (fs::exists(candidate))
        {
            return fs::canonical(candidate);
        }
        if (!current.has_parent_path())
        {
            break;
        }
        current = current.parent_path();
    }
    return std::nullopt;
}

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

    if (avcodec_open2(decoder.codecCtx, codec, nullptr) < 0)
    {
        std::cerr << "[Video] Failed to open codec." << std::endl;
        return false;
    }

    decoder.width = decoder.codecCtx->width;
    decoder.height = decoder.codecCtx->height;
    decoder.frame = av_frame_alloc();
    decoder.rgbaFrame = av_frame_alloc();
    decoder.packet = av_packet_alloc();
    if (!decoder.frame || !decoder.rgbaFrame || !decoder.packet)
    {
        std::cerr << "[Video] Failed to allocate FFmpeg structures." << std::endl;
        return false;
    }

    decoder.bufferSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, decoder.width, decoder.height, 1);
    decoder.swsCtx = sws_getContext(decoder.width,
                                    decoder.height,
                                    decoder.codecCtx->pix_fmt,
                                    decoder.width,
                                    decoder.height,
                                    AV_PIX_FMT_RGBA,
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
    if (decoder.finished)
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
            av_image_fill_arrays(decoder.rgbaFrame->data,
                                 decoder.rgbaFrame->linesize,
                                 rgbaBuffer.data(),
                                 AV_PIX_FMT_RGBA,
                                 decoder.width,
                                 decoder.height,
                                 1);
            sws_scale(decoder.swsCtx,
                      decoder.frame->data,
                      decoder.frame->linesize,
                      0,
                      decoder.height,
                      decoder.rgbaFrame->data,
                      decoder.rgbaFrame->linesize);
            return true;
        }
        else if (receiveResult == AVERROR(EAGAIN))
        {
            continue;
        }
        else if (receiveResult == AVERROR_EOF)
        {
            decoder.finished = true;
            return false;
        }
        else
        {
            std::cerr << "[Video] Decoder error: " << receiveResult << std::endl;
            return false;
        }
    }
}

void cleanupVideoDecoder(VideoDecoder& decoder)
{
    if (decoder.packet)
    {
        av_packet_free(&decoder.packet);
    }
    if (decoder.frame)
    {
        av_frame_free(&decoder.frame);
    }
    if (decoder.rgbaFrame)
    {
        av_frame_free(&decoder.rgbaFrame);
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
