#include "video.h"
#include "engine.h"
#include "display.h"
#include "camera.h"
#include "model.h"
#include "light.h"
#include "utils.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <fstream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
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
    
    // Create initial texture for video
    std::vector<uint8_t> initialFrame(decoder.width * decoder.height * 4, 128); // Gray frame
    primitive->createTextureFromPixelData(
        initialFrame.data(), 
        initialFrame.size(), 
        decoder.width, 
        decoder.height, 
        VK_FORMAT_R8G8B8A8_UNORM
    );

    std::cout << "[Video] Starting video playback loop..." << std::endl;

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
            if (video::decodeNextFrame(decoder, frameBuffer))
            {
                // Update texture with new frame
                primitive->updateTextureFromPixelData(
                    frameBuffer.data(),
                    frameBuffer.size(),
                    decoder.width,
                    decoder.height,
                    VK_FORMAT_R8G8B8A8_UNORM
                );
            }
            else
            {
                std::cout << "[Video] End of video reached" << std::endl;
                break;
            }
            lastFrameTime = currentTime;
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
