#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFont>
#include <QFontDatabase>
#include <QPen>
#include <QBrush>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

struct Args
{
    std::string text = "What";
    std::string output = "/tmp/motive_extrusion_text.png";
    std::string fontPath;
    int pixelHeight = 132;
    float extrudeDepth = 0.20f;
    int canvasWidth = 1920;
    int canvasHeight = 1080;
    int supersample = 2;
    float shearX = -0.32f;
    float rotateDeg = -14.0f;
    float scale = 1.0f;
};

bool parseArgs(int argc, char** argv, Args& args)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> const char*
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
            const char* v = need("--text"); if (!v) return false; args.text = v;
        }
        else if (a == "--out")
        {
            const char* v = need("--out"); if (!v) return false; args.output = v;
        }
        else if (a == "--font")
        {
            const char* v = need("--font"); if (!v) return false; args.fontPath = v;
        }
        else if (a == "--pixel-height")
        {
            const char* v = need("--pixel-height"); if (!v) return false; args.pixelHeight = std::max(8, std::stoi(v));
        }
        else if (a == "--extrude-depth")
        {
            const char* v = need("--extrude-depth"); if (!v) return false; args.extrudeDepth = std::max(0.02f, std::stof(v));
        }
        else if (a == "--canvas-width")
        {
            const char* v = need("--canvas-width"); if (!v) return false; args.canvasWidth = std::max(64, std::stoi(v));
        }
        else if (a == "--canvas-height")
        {
            const char* v = need("--canvas-height"); if (!v) return false; args.canvasHeight = std::max(64, std::stoi(v));
        }
        else if (a == "--supersample")
        {
            const char* v = need("--supersample"); if (!v) return false; args.supersample = std::clamp(std::stoi(v), 1, 4);
        }
        else if (a == "--shear-x")
        {
            const char* v = need("--shear-x"); if (!v) return false; args.shearX = std::stof(v);
        }
        else if (a == "--rotate-deg")
        {
            const char* v = need("--rotate-deg"); if (!v) return false; args.rotateDeg = std::stof(v);
        }
        else if (a == "--scale")
        {
            const char* v = need("--scale"); if (!v) return false; args.scale = std::stof(v);
        }
        else
        {
            std::cerr << "Unknown argument: " << a << std::endl;
            return false;
        }
    }
    return true;
}

QPainterPath centeredTextPath(const QString& text, const QFont& font)
{
    QPainterPath path;
    path.addText(0.0, 0.0, font, text);
    const QRectF b = path.boundingRect();
    QTransform t;
    t.translate(-b.center().x(), -b.center().y());
    return t.map(path);
}

} // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    QGuiApplication app(argc, argv);

    Args args;
    if (!parseArgs(argc, argv, args))
    {
        return 1;
    }

    QFont font;
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setWeight(QFont::DemiBold);
    font.setItalic(false);

    if (!args.fontPath.empty())
    {
        const int id = QFontDatabase::addApplicationFont(QString::fromStdString(args.fontPath));
        if (id >= 0)
        {
            const QStringList fams = QFontDatabase::applicationFontFamilies(id);
            if (!fams.isEmpty())
            {
                font.setFamily(fams.first());
            }
        }
    }

    const int ss = std::max(1, args.supersample);
    const int renderW = args.canvasWidth * ss;
    const int renderH = args.canvasHeight * ss;
    font.setPixelSize(args.pixelHeight * ss);

    const QPainterPath textPath = centeredTextPath(QString::fromStdString(args.text), font);
    if (textPath.isEmpty())
    {
        std::cerr << "Failed to build text path" << std::endl;
        return 2;
    }

    QImage canvas(renderW, renderH, QImage::Format_RGBA8888);
    {
        QPainter bg(&canvas);
        QLinearGradient grad(0.0, 0.0, static_cast<qreal>(renderW), static_cast<qreal>(renderH));
        grad.setColorAt(0.0, QColor(32, 44, 96));
        grad.setColorAt(0.45, QColor(78, 102, 196));
        grad.setColorAt(1.0, QColor(156, 178, 252));
        bg.fillRect(0, 0, renderW, renderH, grad);
    }

    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QTransform scene;
    scene.translate(renderW * 0.56, renderH * 0.63);
    scene.shear(args.shearX, 0.0);
    scene.rotate(args.rotateDeg);
    scene.scale(args.scale * 1.58, args.scale * 1.58);
    p.setTransform(scene);

    const float depthPx = std::max(6.0f, args.extrudeDepth * static_cast<float>(args.pixelHeight * ss) * 1.1f);
    const int layers = std::clamp(static_cast<int>(depthPx / 2.0f), 8, 48);
    const qreal dx = depthPx * 0.62;
    const qreal dy = depthPx * 0.30;

    for (int i = layers; i >= 1; --i)
    {
        const qreal t = static_cast<qreal>(i) / static_cast<qreal>(layers);
        const int r = static_cast<int>(26 + (86 - 26) * (1.0 - t));
        const int g = static_cast<int>(34 + (106 - 34) * (1.0 - t));
        const int b = static_cast<int>(62 + (140 - 62) * (1.0 - t));
        const int a = static_cast<int>(185 + (230 - 185) * (1.0 - t));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(r, g, b, a));

        QTransform extrude;
        extrude.translate(dx * t, dy * t);
        p.drawPath(extrude.map(textPath));
    }

    {
        QLinearGradient face(-260.0, -220.0, 380.0, 280.0);
        face.setColorAt(0.0, QColor(251, 252, 255));
        face.setColorAt(0.55, QColor(238, 241, 249));
        face.setColorAt(1.0, QColor(218, 224, 238));
        p.setBrush(face);
        p.setPen(Qt::NoPen);
        p.drawPath(textPath);

        QPen rim(QColor(255, 255, 255, 145));
        rim.setWidthF(1.35 * ss);
        p.setPen(rim);
        p.setBrush(Qt::NoBrush);
        p.drawPath(textPath);
    }

    p.end();

    QImage output = (ss > 1)
        ? canvas.scaled(args.canvasWidth, args.canvasHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        : canvas;

    if (!output.save(QString::fromStdString(args.output), "PNG"))
    {
        std::cerr << "Failed to save PNG: " << args.output << std::endl;
        return 3;
    }

    std::cout << "Wrote " << args.output << " (" << output.width() << "x" << output.height() << ")" << std::endl;
    return 0;
}
