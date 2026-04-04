#include "asset_browser_widget.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QDrag>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QDebug>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <glm/glm.hpp>
#include <algorithm>
#include <limits>
#include <vector>

#include "ufbx.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

namespace motive::ui {
namespace {

QIcon folderSequenceIcon(const QSize& size = QSize(48, 48))
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#314968")));
    painter.drawRoundedRect(QRect(4, 10, size.width() - 8, size.height() - 14), 8, 8);
    painter.setBrush(QColor(QStringLiteral("#4b6f99")));
    painter.drawRoundedRect(QRect(8, 5, size.width() / 2, 10), 6, 6);
    painter.setBrush(QColor(QStringLiteral("#dbe8ff")));
    for (int i = 0; i < 3; ++i)
    {
        painter.drawRoundedRect(QRect(11 + (i * 10), 19 + (i * 3), 14, 18), 3, 3);
    }

    painter.setPen(QColor(QStringLiteral("#eef5ff")));
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSizeF(8.5);
    painter.setFont(font);
    painter.drawText(QRect(0, size.height() - 17, size.width(), 12), Qt::AlignCenter, QStringLiteral("SEQ"));
    return QIcon(pixmap);
}

bool isImageFilePath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("png") ||
           suffix == QStringLiteral("jpg") ||
           suffix == QStringLiteral("jpeg") ||
           suffix == QStringLiteral("webp");
}

bool isVideoFilePath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("mp4") ||
           suffix == QStringLiteral("mov") ||
           suffix == QStringLiteral("mkv") ||
           suffix == QStringLiteral("avi") ||
           suffix == QStringLiteral("webm") ||
           suffix == QStringLiteral("m4v");
}

bool isRenderableModelFilePath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("fbx") ||
           suffix == QStringLiteral("gltf") ||
           suffix == QStringLiteral("glb");
}

bool isPreviewableFilePath(const QString& path)
{
    return isImageFilePath(path) || isVideoFilePath(path) || isRenderableModelFilePath(path);
}

glm::vec3 toGlmVec3(ufbx_vec3 value)
{
    return glm::vec3(static_cast<float>(value.x),
                     static_cast<float>(value.y),
                     static_cast<float>(value.z));
}

QPixmap modelPreviewCard(const QFileInfo& info, const QSize& size = QSize(480, 300))
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(QPointF(0.0, 0.0), QPointF(size.width(), size.height()));
    bg.setColorAt(0.0, QColor(QStringLiteral("#0d1822")));
    bg.setColorAt(1.0, QColor(QStringLiteral("#182938")));
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawRoundedRect(QRectF(0, 0, size.width(), size.height()), 18, 18);

    painter.setBrush(QColor(QStringLiteral("#22384d")));
    painter.drawRoundedRect(QRectF(26, 24, size.width() - 52, size.height() - 48), 16, 16);

    painter.setPen(QColor(QStringLiteral("#7ec8ff")));
    QFont badgeFont = painter.font();
    badgeFont.setBold(true);
    badgeFont.setPointSizeF(18.0);
    painter.setFont(badgeFont);
    painter.drawText(QRectF(42, 40, size.width() - 84, 32), Qt::AlignLeft | Qt::AlignVCenter,
                     info.suffix().toUpper());

    painter.setPen(QColor(QStringLiteral("#edf5ff")));
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(15.0);
    painter.setFont(titleFont);
    painter.drawText(QRectF(42, 88, size.width() - 84, 56),
                     Qt::AlignLeft | Qt::TextWordWrap,
                     info.completeBaseName());

    painter.setPen(QColor(QStringLiteral("#a9c0d6")));
    QFont metaFont = painter.font();
    metaFont.setBold(false);
    metaFont.setPointSizeF(11.0);
    painter.setFont(metaFont);
    painter.drawText(QRectF(42, size.height() - 88, size.width() - 84, 48),
                     Qt::AlignLeft | Qt::TextWordWrap,
                     QStringLiteral("3D asset preview\nDirect viewport load supported"));

    painter.setPen(QPen(QColor(QStringLiteral("#8ed0ff")), 2.0));
    painter.setBrush(Qt::NoBrush);
    const QPointF center(size.width() * 0.72, size.height() * 0.48);
    const qreal radius = qMin(size.width(), size.height()) * 0.18;
    painter.drawEllipse(center, radius, radius);
    painter.drawLine(QPointF(center.x(), center.y() - radius),
                     QPointF(center.x(), center.y() + radius));
    painter.drawLine(QPointF(center.x() - radius, center.y()),
                     QPointF(center.x() + radius, center.y()));
    painter.drawLine(QPointF(center.x() - radius * 0.7, center.y() - radius * 0.7),
                     QPointF(center.x() + radius * 0.7, center.y() + radius * 0.7));

    return pixmap;
}

struct PreviewTriangle
{
    glm::vec3 a;
    glm::vec3 b;
    glm::vec3 c;
};

std::vector<PreviewTriangle> loadFbxPreviewTriangles(const QString& filePath)
{
    std::vector<PreviewTriangle> triangles;
    ufbx_load_opts opts = {};
    opts.generate_missing_normals = true;
    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(filePath.toUtf8().constData(), &opts, &error);
    if (!scene)
    {
        return triangles;
    }

    for (size_t meshIndex = 0; meshIndex < scene->meshes.count; ++meshIndex)
    {
        const ufbx_mesh* mesh = scene->meshes.data[meshIndex];
        if (!mesh || !mesh->vertex_position.exists)
        {
            continue;
        }

        std::vector<uint32_t> triIndices(mesh->max_face_triangles * 3);
        for (size_t faceIndex = 0; faceIndex < mesh->faces.count; ++faceIndex)
        {
            const ufbx_face face = mesh->faces.data[faceIndex];
            const uint32_t numTriangles = ufbx_triangulate_face(triIndices.data(), triIndices.size(), mesh, face);
            for (uint32_t tri = 0; tri < numTriangles; ++tri)
            {
                const size_t i0 = triIndices[tri * 3 + 0];
                const size_t i1 = triIndices[tri * 3 + 1];
                const size_t i2 = triIndices[tri * 3 + 2];
                triangles.push_back(PreviewTriangle{
                    toGlmVec3(ufbx_get_vertex_vec3(&mesh->vertex_position, i0)),
                    toGlmVec3(ufbx_get_vertex_vec3(&mesh->vertex_position, i1)),
                    toGlmVec3(ufbx_get_vertex_vec3(&mesh->vertex_position, i2)),
                });
            }
        }
    }

    ufbx_free_scene(scene);
    return triangles;
}

QPixmap renderFbxPreview(const QString& filePath, const QSize& size = QSize(480, 300))
{
    const std::vector<PreviewTriangle> triangles = loadFbxPreviewTriangles(filePath);
    if (triangles.empty())
    {
        return {};
    }

    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
    for (const PreviewTriangle& tri : triangles)
    {
        minBounds = glm::min(minBounds, glm::min(tri.a, glm::min(tri.b, tri.c)));
        maxBounds = glm::max(maxBounds, glm::max(tri.a, glm::max(tri.b, tri.c)));
    }

    const glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    const glm::vec3 extent = maxBounds - minBounds;
    const float maxExtent = std::max({extent.x, extent.y, extent.z, 1e-4f});
    const float scale = 1.75f / maxExtent;

    const float yaw = -0.7f;
    const float pitch = 0.45f;
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cx = std::cos(pitch);
    const float sx = std::sin(pitch);
    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, 0.6f, 0.7f));

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(QStringLiteral("#0a1118")));
    QPainter bgPainter(&image);
    QLinearGradient bg(QPointF(0.0, 0.0), QPointF(size.width(), size.height()));
    bg.setColorAt(0.0, QColor(QStringLiteral("#0b1620")));
    bg.setColorAt(1.0, QColor(QStringLiteral("#1a2e42")));
    bgPainter.fillRect(image.rect(), bg);
    bgPainter.end();

    std::vector<float> depth(static_cast<size_t>(size.width() * size.height()),
                             std::numeric_limits<float>::infinity());

    auto rotatePoint = [&](glm::vec3 p) {
        p = (p - center) * scale;
        const float x1 = p.x * cy + p.z * sy;
        const float z1 = -p.x * sy + p.z * cy;
        p.x = x1;
        p.z = z1;
        const float y2 = p.y * cx - p.z * sx;
        const float z2 = p.y * sx + p.z * cx;
        p.y = y2;
        p.z = z2;
        return p;
    };

    struct ScreenVertex
    {
        float x;
        float y;
        float z;
    };

    for (const PreviewTriangle& srcTri : triangles)
    {
        const glm::vec3 a3 = rotatePoint(srcTri.a);
        const glm::vec3 b3 = rotatePoint(srcTri.b);
        const glm::vec3 c3 = rotatePoint(srcTri.c);

        const glm::vec3 normal = glm::normalize(glm::cross(b3 - a3, c3 - a3));
        if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z))
        {
            continue;
        }

        const float shade = std::clamp(glm::dot(normal, lightDir) * 0.6f + 0.4f, 0.15f, 1.0f);
        const QColor color = QColor::fromRgbF(0.35f * shade, 0.68f * shade, 0.95f * shade, 1.0f);

        auto project = [&](const glm::vec3& p) -> ScreenVertex {
            const float perspective = 1.8f / (p.z + 3.2f);
            const float sxp = p.x * perspective * size.width() * 0.42f + size.width() * 0.5f;
            const float syp = -p.y * perspective * size.height() * 0.42f + size.height() * 0.56f;
            return {sxp, syp, p.z};
        };

        const ScreenVertex a = project(a3);
        const ScreenVertex b = project(b3);
        const ScreenVertex c = project(c3);

        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::abs(area) < 1e-5f)
        {
            continue;
        }

        const int minX = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
        const int maxX = std::min(size.width() - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
        const int maxY = std::min(size.height() - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));

        for (int y = minY; y <= maxY; ++y)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;
                const float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
                const float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                {
                    continue;
                }

                const float z = w0 * a.z + w1 * b.z + w2 * c.z;
                const int idx = y * size.width() + x;
                if (z >= depth[static_cast<size_t>(idx)])
                {
                    continue;
                }
                depth[static_cast<size_t>(idx)] = z;
                image.setPixelColor(x, y, color);
            }
        }
    }

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(QStringLiteral("#d9f1ff")), 1.2));
    painter.drawRoundedRect(image.rect().adjusted(0, 0, -1, -1), 16, 16);
    painter.end();

    return QPixmap::fromImage(image);
}

QImage decodeFirstVideoFrame(const QString& filePath)
{
    AVFormatContext* formatCtx = nullptr;
    if (avformat_open_input(&formatCtx, QFile::encodeName(filePath).constData(), nullptr, nullptr) < 0)
    {
        return {};
    }

    if (avformat_find_stream_info(formatCtx, nullptr) < 0)
    {
        avformat_close_input(&formatCtx);
        return {};
    }

    int videoStreamIndex = -1;
    for (unsigned i = 0; i < formatCtx->nb_streams; ++i)
    {
        if (formatCtx->streams[i] &&
            formatCtx->streams[i]->codecpar &&
            formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStreamIndex = static_cast<int>(i);
            break;
        }
    }

    if (videoStreamIndex < 0)
    {
        avformat_close_input(&formatCtx);
        return {};
    }

    AVStream* stream = formatCtx->streams[videoStreamIndex];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder)
    {
        avformat_close_input(&formatCtx);
        return {};
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(decoder);
    if (!codecCtx)
    {
        avformat_close_input(&formatCtx);
        return {};
    }

    if (avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0 ||
        avcodec_open2(codecCtx, decoder, nullptr) < 0)
    {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return {};
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgbaFrame = av_frame_alloc();
    SwsContext* sws = nullptr;
    QImage result;

    auto cleanup = [&]()
    {
        sws_freeContext(sws);
        av_frame_free(&rgbaFrame);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
    };

    if (!packet || !frame || !rgbaFrame)
    {
        cleanup();
        return {};
    }

    auto decodeFrame = [&](AVFrame* decodedFrame)
    {
        sws = sws_getCachedContext(
            sws,
            decodedFrame->width,
            decodedFrame->height,
            static_cast<AVPixelFormat>(decodedFrame->format),
            decodedFrame->width,
            decodedFrame->height,
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr);
        if (!sws)
        {
            return;
        }

        av_frame_unref(rgbaFrame);
        rgbaFrame->format = AV_PIX_FMT_RGBA;
        rgbaFrame->width = decodedFrame->width;
        rgbaFrame->height = decodedFrame->height;
        if (av_frame_get_buffer(rgbaFrame, 32) < 0 || av_frame_make_writable(rgbaFrame) < 0)
        {
            return;
        }

        sws_scale(sws,
                  decodedFrame->data,
                  decodedFrame->linesize,
                  0,
                  decodedFrame->height,
                  rgbaFrame->data,
                  rgbaFrame->linesize);

        QImage image(rgbaFrame->data[0],
                     decodedFrame->width,
                     decodedFrame->height,
                     rgbaFrame->linesize[0],
                     QImage::Format_RGBA8888);
        result = image.copy();
    };

    while (av_read_frame(formatCtx, packet) >= 0 && result.isNull())
    {
        if (packet->stream_index == videoStreamIndex && avcodec_send_packet(codecCtx, packet) >= 0)
        {
            while (avcodec_receive_frame(codecCtx, frame) >= 0 && result.isNull())
            {
                if (frame->width > 0 && frame->height > 0)
                {
                    decodeFrame(frame);
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(packet);
    }

    cleanup();
    return result;
}

class SequenceAwareIconProvider final : public QFileIconProvider
{
public:
    QIcon icon(const QFileInfo& info) const override
    {
        Q_UNUSED(info);
        return QFileIconProvider::icon(info);
    }
};

}  // namespace

AssetBrowserWidget::AssetBrowserWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(220);
    buildUi();
    setRootPath(QDir::currentPath());
}

AssetBrowserWidget::~AssetBrowserWidget() = default;

void AssetBrowserWidget::setRootPath(const QString& path)
{
    QString resolvedPath = path;
    if (resolvedPath.isEmpty() || !QFileInfo::exists(resolvedPath) || !QFileInfo(resolvedPath).isDir())
    {
        resolvedPath = QDir::currentPath();
    }

    m_rootPath = QDir(resolvedPath).absolutePath();
    if (m_fsModel && m_tree)
    {
        const QModelIndex rootIndex = m_fsModel->setRootPath(m_rootPath);
        m_tree->setRootIndex(rootIndex);
    }
    if (m_rootPathLabel)
    {
        m_rootPathLabel->setText(QDir::toNativeSeparators(m_rootPath));
    }
    if (!m_galleryPath.isEmpty())
    {
        setGalleryPath(m_galleryPath);
    }

    if (m_rootPathChangedCallback)
    {
        m_rootPathChangedCallback(m_rootPath);
    }
}

QString AssetBrowserWidget::rootPath() const
{
    return m_rootPath;
}

QString AssetBrowserWidget::galleryPath() const
{
    return m_galleryPath;
}

QString AssetBrowserWidget::selectedAssetPath() const
{
    return m_lastActivated.filePath;
}

void AssetBrowserWidget::restoreGalleryPath(const QString& path)
{
    if (path.isEmpty())
    {
        return;
    }
    setGalleryPath(path);
}

void AssetBrowserWidget::setActivationCallback(std::function<void(const AssetBrowserSelection&)> callback)
{
    m_activationCallback = std::move(callback);
}

void AssetBrowserWidget::setPreviewAnchorWidget(QWidget* widget)
{
    m_previewAnchorWidget = widget;
}

void AssetBrowserWidget::setRootPathChangedCallback(std::function<void(const QString&)> callback)
{
    m_rootPathChangedCallback = std::move(callback);
}

void AssetBrowserWidget::buildUi()
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* pane = new QWidget(this);
    pane->setStyleSheet(
        QStringLiteral(
            "QWidget { background: #10161d; color: #edf2f7; }"
            "QPushButton, QToolButton { background: #1b2430; border: 1px solid #2e3b4a; border-radius: 7px; padding: 6px 10px; }"
            "QPushButton:hover, QToolButton:hover { background: #233142; }"
            "QTreeView, QListWidget { background: #0c1015; border: 1px solid #202934; border-radius: 10px; }"));
    outer->addWidget(pane);

    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);
    m_rootButton = new QPushButton(QStringLiteral("Root..."), pane);
    m_refreshButton = new QToolButton(pane);
    m_refreshButton->setText(QStringLiteral("Refresh"));
    toolbar->addWidget(m_rootButton, 1);
    toolbar->addWidget(m_refreshButton);
    layout->addLayout(toolbar);

    m_rootPathLabel = new QLabel(pane);
    m_rootPathLabel->setWordWrap(true);
    layout->addWidget(m_rootPathLabel);

    m_stack = new QStackedWidget(pane);
    m_stack->addWidget(buildTreePage());
    m_stack->addWidget(buildGalleryPage());
    layout->addWidget(m_stack, 1);

    connect(m_rootButton, &QPushButton::clicked, this, [this]()
    {
        const QString selected = QFileDialog::getExistingDirectory(
            this,
            QStringLiteral("Select Media Folder"),
            m_rootPath.isEmpty() ? QDir::currentPath() : m_rootPath,
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
        if (!selected.isEmpty())
        {
            setRootPath(selected);
        }
    });

    connect(m_refreshButton, &QToolButton::clicked, this, [this]()
    {
        setRootPath(m_rootPath);
    });
}

QWidget* AssetBrowserWidget::buildTreePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    m_fsModel->setIconProvider(new SequenceAwareIconProvider);

    m_tree = new QTreeView(page);
    m_tree->setModel(m_fsModel);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setDragEnabled(true);
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setMouseTracking(true);
    m_tree->header()->setStretchLastSection(true);
    m_tree->viewport()->installEventFilter(this);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& index)
    {
        if (!index.isValid() || !m_fsModel)
        {
            return;
        }
        const QFileInfo info = m_fsModel->fileInfo(index);
        if (info.isDir())
        {
            setGalleryPath(info.absoluteFilePath());
            return;
        }
        activatePath(info.absoluteFilePath());
    });

    connect(m_tree, &QTreeView::entered, this, [this](const QModelIndex& index)
    {
        if (!index.isValid() || !m_fsModel)
        {
            hideHoverPreview();
            return;
        }
        const QFileInfo info = m_fsModel->fileInfo(index);
        if (info.isFile())
        {
            showHoverPreview(info.absoluteFilePath());
        }
        else
        {
            hideHoverPreview();
        }
    });

    return page;
}

QWidget* AssetBrowserWidget::buildGalleryPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* topRow = new QHBoxLayout;
    m_galleryBackButton = new QToolButton(page);
    m_galleryBackButton->setText(QStringLiteral("Back"));
    m_galleryTitleLabel = new QLabel(page);
    m_galleryTitleLabel->setWordWrap(true);
    topRow->addWidget(m_galleryBackButton);
    topRow->addWidget(m_galleryTitleLabel, 1);
    layout->addLayout(topRow);

    m_galleryList = new QListWidget(page);
    m_galleryList->setViewMode(QListView::IconMode);
    m_galleryList->setResizeMode(QListView::Adjust);
    m_galleryList->setMovement(QListView::Static);
    m_galleryList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_galleryList->setDragEnabled(true);
    m_galleryList->setDragDropMode(QAbstractItemView::DragOnly);
    m_galleryList->setIconSize(QSize(48, 48));
    m_galleryList->setSpacing(8);
    m_galleryList->setMouseTracking(true);
    m_galleryList->viewport()->installEventFilter(this);
    layout->addWidget(m_galleryList, 1);

    connect(m_galleryBackButton, &QToolButton::clicked, this, [this]()
    {
        m_galleryPath.clear();
        if (m_stack)
        {
            m_stack->setCurrentIndex(0);
        }
        hideHoverPreview();
    });

    connect(m_galleryList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item)
    {
        if (!item)
        {
            return;
        }
        const QString path = item->data(Qt::UserRole).toString();
        const QFileInfo info(path);
        if (info.isDir())
        {
            setGalleryPath(path);
            return;
        }
        activatePath(path);
    });

    connect(m_galleryList, &QListWidget::itemEntered, this, [this](QListWidgetItem* item)
    {
        if (!item)
        {
            hideHoverPreview();
            return;
        }
        const QString path = item->data(Qt::UserRole).toString();
        const QFileInfo info(path);
        if (info.isFile())
        {
            showHoverPreview(path);
        }
        else
        {
            hideHoverPreview();
        }
    });

    return page;
}

void AssetBrowserWidget::setGalleryPath(const QString& path)
{
    if (!m_galleryList || !m_stack)
    {
        return;
    }

    if (path.isEmpty() || !QFileInfo(path).isDir())
    {
        m_galleryPath.clear();
        m_stack->setCurrentIndex(0);
        return;
    }

    m_galleryPath = QDir(path).absolutePath();
    populateGallery();
    m_stack->setCurrentIndex(1);
}

void AssetBrowserWidget::populateGallery()
{
    m_galleryList->clear();
    m_galleryTitleLabel->setText(QDir::toNativeSeparators(m_galleryPath));

    QDir dir(m_galleryPath);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    SequenceAwareIconProvider iconProvider;
    for (const QFileInfo& info : entries)
    {
        QIcon itemIcon = info.isDir() ? iconProvider.icon(info) : QIcon(previewPixmapForFile(info.absoluteFilePath()));
        if (info.isDir() && itemIcon.isNull())
        {
            itemIcon = folderSequenceIcon();
        }

        auto* item = new QListWidgetItem(itemIcon, info.fileName(), m_galleryList);
        item->setData(Qt::UserRole, info.absoluteFilePath());
        item->setToolTip(QDir::toNativeSeparators(info.absoluteFilePath()));
    }
}

QPixmap AssetBrowserWidget::previewPixmapForFile(const QString& filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile())
    {
        return {};
    }

    const QString cacheKey = info.absoluteFilePath() + QLatin1Char('|') +
                             QString::number(info.lastModified().toMSecsSinceEpoch());
    const auto it = m_previewPixmapCache.constFind(cacheKey);
    if (it != m_previewPixmapCache.constEnd())
    {
        return it.value();
    }

    QPixmap pixmap;
    if (isImageFilePath(filePath))
    {
        QImage image(filePath);
        if (!image.isNull())
        {
            pixmap = QPixmap::fromImage(image);
        }
    }
    else if (isVideoFilePath(filePath))
    {
        const QImage frame = decodeFirstVideoFrame(filePath);
        if (!frame.isNull())
        {
            pixmap = QPixmap::fromImage(frame);
        }
    }
    else if (isRenderableModelFilePath(filePath))
    {
        if (info.suffix().compare(QStringLiteral("fbx"), Qt::CaseInsensitive) == 0)
        {
            pixmap = renderFbxPreview(filePath);
        }
        if (pixmap.isNull())
        {
            pixmap = modelPreviewCard(info);
        }
    }

    if (!pixmap.isNull())
    {
        m_previewPixmapCache.insert(cacheKey, pixmap);
    }
    return pixmap;
}

void AssetBrowserWidget::showHoverPreview(const QString& filePath)
{
    if (!isPreviewableFilePath(filePath))
    {
        hideHoverPreview();
        return;
    }

    const QPixmap source = previewPixmapForFile(filePath);
    if (source.isNull())
    {
        hideHoverPreview();
        return;
    }

    if (!m_hoverPreview)
    {
        m_hoverPreview = new QLabel(nullptr, Qt::ToolTip);
        m_hoverPreview->setObjectName(QStringLiteral("assetBrowserHoverPreview"));
        m_hoverPreview->setAlignment(Qt::AlignCenter);
        m_hoverPreview->setStyleSheet(
            QStringLiteral(
                "QLabel#assetBrowserHoverPreview { "
                "background: #05080c; color: #edf2f7; border: 1px solid #24303c; "
                "border-radius: 10px; padding: 8px; }"));
    }

    QSize targetSize(280, 180);
    if (m_previewAnchorWidget)
    {
        const QSize candidate = m_previewAnchorWidget->size() - QSize(32, 32);
        if (candidate.width() > 0 && candidate.height() > 0)
        {
            targetSize = candidate;
        }
    }

    const QPixmap scaled = source.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_hoverPreview->setPixmap(scaled);
    m_hoverPreview->resize(scaled.size() + QSize(16, 16));

    QPoint anchor(80, 80);
    if (m_previewAnchorWidget)
    {
        const QRect previewRect = m_previewAnchorWidget->rect();
        anchor = m_previewAnchorWidget->mapToGlobal(
            QPoint(qMax(24, (previewRect.width() - m_hoverPreview->width()) / 2),
                   qMax(24, previewRect.height() / 8)));
    }
    m_hoverPreview->move(anchor);
    m_hoverPreview->show();
}

void AssetBrowserWidget::hideHoverPreview()
{
    if (m_hoverPreview)
    {
        m_hoverPreview->hide();
    }
}

void AssetBrowserWidget::activatePath(const QString& filePath)
{
    m_lastActivated.filePath = filePath;
    m_lastActivated.directoryPath = QFileInfo(filePath).absolutePath();
    m_lastActivated.displayName = QFileInfo(filePath).fileName();
    m_lastActivated.isDirectory = QFileInfo(filePath).isDir();
    if (m_activationCallback)
    {
        m_activationCallback(m_lastActivated);
    }
}

bool AssetBrowserWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == (m_tree ? m_tree->viewport() : nullptr))
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                m_treeDragStartPos = mouseEvent->pos();
            }
        }
        else if (event->type() == QEvent::MouseMove && m_tree && m_fsModel)
        {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->buttons() & Qt::LeftButton)
            {
                if ((mouseEvent->pos() - m_treeDragStartPos).manhattanLength() >= QApplication::startDragDistance())
                {
                    const QModelIndex index = m_tree->indexAt(m_treeDragStartPos);
                    if (index.isValid())
                    {
                        const QFileInfo info = m_fsModel->fileInfo(index);
                        if (info.exists() && info.isFile())
                        {
                            hideHoverPreview();
                            qDebug() << "[AssetBrowser] Starting tree drag for" << info.absoluteFilePath();
                            auto* mimeData = new QMimeData;
                            mimeData->setUrls({QUrl::fromLocalFile(info.absoluteFilePath())});
                            auto* drag = new QDrag(m_tree);
                            drag->setMimeData(mimeData);
                            drag->exec(Qt::CopyAction);
                            return true;
                        }
                    }
                }
            }
        }
        else if (event->type() == QEvent::Leave)
        {
            hideHoverPreview();
        }
    }

    if (watched == (m_galleryList ? m_galleryList->viewport() : nullptr))
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                m_galleryDragStartPos = mouseEvent->pos();
            }
        }
        else if (event->type() == QEvent::MouseMove && m_galleryList)
        {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->buttons() & Qt::LeftButton)
            {
                if ((mouseEvent->pos() - m_galleryDragStartPos).manhattanLength() >= QApplication::startDragDistance())
                {
                    if (QListWidgetItem* item = m_galleryList->itemAt(m_galleryDragStartPos))
                    {
                        const QString path = item->data(Qt::UserRole).toString();
                        const QFileInfo info(path);
                        if (info.exists() && info.isFile())
                        {
                            hideHoverPreview();
                            qDebug() << "[AssetBrowser] Starting gallery drag for" << info.absoluteFilePath();
                            auto* mimeData = new QMimeData;
                            mimeData->setUrls({QUrl::fromLocalFile(info.absoluteFilePath())});
                            auto* drag = new QDrag(m_galleryList);
                            drag->setMimeData(mimeData);
                            drag->exec(Qt::CopyAction);
                            return true;
                        }
                    }
                }
            }
        }
        else if (event->type() == QEvent::Leave)
        {
            hideHoverPreview();
        }
    }

    return QWidget::eventFilter(watched, event);
}

}  // namespace motive::ui
