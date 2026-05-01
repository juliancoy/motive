#include <QCoreApplication>
#include <QColor>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QTransform>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "text_rendering.h"

namespace {

struct Args
{
    std::string text = "What";
    std::string output = "/tmp/motive_oblique_text.png";
    std::string fontPath;
    int pixelHeight = 96;
    float shearX = -0.65f;
    float rotateDeg = -20.0f;
    float scale = 2.0f;
    int canvasWidth = 1600;
    int canvasHeight = 900;
    int supersample = 2;
};

bool parseArgs(int argc, char** argv, Args& args)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto needValue = [&](const char* flag) -> const char*
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for " << flag << std::endl;
                return nullptr;
            }
            return argv[++i];
        };

        if (a == "--text")
        {
            const char* v = needValue("--text");
            if (!v) return false;
            args.text = v;
        }
        else if (a == "--out")
        {
            const char* v = needValue("--out");
            if (!v) return false;
            args.output = v;
        }
        else if (a == "--font")
        {
            const char* v = needValue("--font");
            if (!v) return false;
            args.fontPath = v;
        }
        else if (a == "--pixel-height")
        {
            const char* v = needValue("--pixel-height");
            if (!v) return false;
            args.pixelHeight = std::max(8, std::stoi(v));
        }
        else if (a == "--shear-x")
        {
            const char* v = needValue("--shear-x");
            if (!v) return false;
            args.shearX = std::stof(v);
        }
        else if (a == "--rotate-deg")
        {
            const char* v = needValue("--rotate-deg");
            if (!v) return false;
            args.rotateDeg = std::stof(v);
        }
        else if (a == "--scale")
        {
            const char* v = needValue("--scale");
            if (!v) return false;
            args.scale = std::stof(v);
        }
        else if (a == "--canvas-width")
        {
            const char* v = needValue("--canvas-width");
            if (!v) return false;
            args.canvasWidth = std::max(64, std::stoi(v));
        }
        else if (a == "--canvas-height")
        {
            const char* v = needValue("--canvas-height");
            if (!v) return false;
            args.canvasHeight = std::max(64, std::stoi(v));
        }
        else if (a == "--supersample")
        {
            const char* v = needValue("--supersample");
            if (!v) return false;
            args.supersample = std::clamp(std::stoi(v), 1, 4);
        }
        else if (a == "--help" || a == "-h")
        {
            std::cout << "Usage: text_oblique_png [options]\n"
                      << "  --text <string>\n"
                      << "  --out <path.png>\n"
                      << "  --font <path.ttf>\n"
                      << "  --pixel-height <int>\n"
                      << "  --shear-x <float>\n"
                      << "  --rotate-deg <float>\n"
                      << "  --scale <float>\n"
                      << "  --canvas-width <int>\n"
                      << "  --canvas-height <int>\n"
                      << "  --supersample <1-4>\n";
            return false;
        }
        else
        {
            std::cerr << "Unknown argument: " << a << std::endl;
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    Args args;
    if (!parseArgs(argc, argv, args))
    {
        return 1;
    }

    motive::text::FontRenderOptions fontOptions;
    fontOptions.fontPath = args.fontPath;

    const int ss = std::max(1, args.supersample);
    const motive::text::FontBitmap bitmap = motive::text::renderText(
        args.text,
        static_cast<uint32_t>(args.pixelHeight * ss),
        fontOptions);
    if (bitmap.width == 0 || bitmap.height == 0 || bitmap.pixels.empty())
    {
        std::cerr << "Failed to rasterize text" << std::endl;
        return 2;
    }

    QImage src(static_cast<int>(bitmap.width), static_cast<int>(bitmap.height), QImage::Format_RGBA8888);
    if (src.bytesPerLine() != static_cast<int>(bitmap.width * 4u))
    {
        std::cerr << "Unexpected stride in source image" << std::endl;
        return 3;
    }
    std::memcpy(src.bits(), bitmap.pixels.data(), bitmap.pixels.size());

    const int renderWidth = args.canvasWidth * ss;
    const int renderHeight = args.canvasHeight * ss;
    QImage canvas(renderWidth, renderHeight, QImage::Format_RGBA8888);

    QPainter bgPainter(&canvas);
    QLinearGradient bg(0.0, 0.0, static_cast<double>(renderWidth), static_cast<double>(renderHeight));
    bg.setColorAt(0.0, QColor(38, 51, 97));
    bg.setColorAt(0.35, QColor(80, 102, 196));
    bg.setColorAt(1.0, QColor(166, 184, 255));
    bgPainter.fillRect(0, 0, renderWidth, renderHeight, bg);
    bgPainter.end();

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::LosslessImageRendering, true);

    QTransform xform;
    const float cx = static_cast<float>(renderWidth) * 0.5f;
    const float cy = static_cast<float>(renderHeight) * 0.58f;
    xform.translate(cx, cy);
    xform.shear(args.shearX, 0.0f);
    xform.rotate(args.rotateDeg);
    xform.scale(args.scale, args.scale);
    xform.translate(-static_cast<float>(src.width()) * 0.5f, -static_cast<float>(src.height()) * 0.5f);

    painter.setTransform(xform);

    // Shadow layers.
    for (int i = 0; i < 9; ++i)
    {
        QImage shadow = src.copy();
        const uint8_t alphaScale = static_cast<uint8_t>(80 - i * 7);
        for (int y = 0; y < shadow.height(); ++y)
        {
            uint8_t* row = shadow.scanLine(y);
            for (int x = 0; x < shadow.width(); ++x)
            {
                uint8_t* px = row + x * 4;
                const uint8_t a = static_cast<uint8_t>((static_cast<uint16_t>(px[3]) * alphaScale) / 255u);
                px[0] = 18;  // R
                px[1] = 24;  // G
                px[2] = 44;  // B
                px[3] = a;
            }
        }
        const qreal ox = 10.0 + static_cast<qreal>(i) * 2.1;
        const qreal oy = 14.0 + static_cast<qreal>(i) * 1.6;
        painter.drawImage(QPointF(ox, oy), shadow);
    }

    // Main fill.
    QImage fill = src.copy();
    for (int y = 0; y < fill.height(); ++y)
    {
        uint8_t* row = fill.scanLine(y);
        const float t = static_cast<float>(y) / std::max(1, fill.height() - 1);
        const int r = static_cast<int>(243.0f + (255.0f - 243.0f) * t);
        const int g = static_cast<int>(246.0f + (252.0f - 246.0f) * t);
        const int b = static_cast<int>(255.0f);
        for (int x = 0; x < fill.width(); ++x)
        {
            uint8_t* px = row + x * 4;
            px[0] = static_cast<uint8_t>(r);
            px[1] = static_cast<uint8_t>(g);
            px[2] = static_cast<uint8_t>(b);
        }
    }
    painter.drawImage(0, 0, fill);
    painter.end();

    QImage output = (ss > 1)
        ? canvas.scaled(args.canvasWidth, args.canvasHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        : canvas;

    if (!output.save(QString::fromStdString(args.output), "PNG"))
    {
        std::cerr << "Failed to save PNG: " << args.output << std::endl;
        return 4;
    }

    std::cout << "Wrote " << args.output << " (" << output.width() << "x" << output.height() << ")" << std::endl;
    return 0;
}
