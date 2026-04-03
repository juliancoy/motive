#include "engine.h"
#include "engine_ui_project_session.h"
#include "model.h"

#include <GLFW/glfw3.h>
#include <../tinygltf/tiny_gltf.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ufbx.h"

namespace {

bool isProfiledAsset(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("fbx") ||
           suffix == QStringLiteral("gltf") ||
           suffix == QStringLiteral("glb");
}

std::vector<QString> sceneAssetPathsFromCurrentProject()
{
    motive::ui::ProjectSession session;
    std::vector<QString> paths;
    std::set<std::string> seen;

    const QJsonArray items = session.currentSceneItems();
    for (const QJsonValue& value : items)
    {
        const QJsonObject object = value.toObject();
        const QString path = object.value(QStringLiteral("sourcePath")).toString();
        if (path.isEmpty() || !isProfiledAsset(path))
        {
            continue;
        }
        const std::string key = QFileInfo(path).absoluteFilePath().toStdString();
        if (seen.insert(key).second)
        {
            paths.push_back(QFileInfo(path).absoluteFilePath());
        }
    }

    if (!paths.empty())
    {
        return paths;
    }

    const QString fallbackFbx = QFileInfo(QStringLiteral("thirdpersonshooter/55-rp_nathan_animated_003_walking_fbx/rp_nathan_animated_003_walking.fbx")).absoluteFilePath();
    const QString fallbackGltf = QFileInfo(QStringLiteral("thirdpersonshooter/city/scene.gltf")).absoluteFilePath();
    if (QFileInfo::exists(fallbackFbx))
    {
        paths.push_back(fallbackFbx);
    }
    if (QFileInfo::exists(fallbackGltf))
    {
        paths.push_back(fallbackGltf);
    }
    return paths;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const std::vector<QString> assetPaths = sceneAssetPathsFromCurrentProject();
    if (assetPaths.empty())
    {
        std::cerr << "No current FBX/GLTF scene assets found to profile." << std::endl;
        return 1;
    }

    try
    {
#ifdef GLFW_PLATFORM
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
#endif
        Engine engine;
        double totalMs = 0.0;

        std::cout << "Profiling current scene asset loads:" << std::endl;
        for (const QString& path : assetPaths)
        {
            const QFileInfo info(path);
            const QString suffix = info.suffix().toLower();

            double parseMs = 0.0;
            double constructMs = 0.0;
            double scaleMs = 0.0;

            if (suffix == QStringLiteral("fbx"))
            {
                const auto parseStart = std::chrono::steady_clock::now();
                ufbx_load_opts opts = {};
                opts.generate_missing_normals = true;
                ufbx_error error;
                ufbx_scene* scene = ufbx_load_file(path.toUtf8().constData(), &opts, &error);
                const auto parseEnd = std::chrono::steady_clock::now();
                parseMs = std::chrono::duration<double, std::milli>(parseEnd - parseStart).count();
                if (!scene)
                {
                    std::cerr << "  " << path.toStdString() << " | parse failed: "
                              << std::string(error.description.data, error.description.length) << std::endl;
                    continue;
                }
                ufbx_free_scene(scene);
            }
            else
            {
                const auto parseStart = std::chrono::steady_clock::now();
                tinygltf::TinyGLTF loader;
                tinygltf::Model gltfModel;
                std::string err;
                std::string warn;
                bool ok = false;
                if (suffix == QStringLiteral("glb"))
                {
                    ok = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, path.toStdString());
                }
                else
                {
                    ok = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, path.toStdString());
                }
                const auto parseEnd = std::chrono::steady_clock::now();
                parseMs = std::chrono::duration<double, std::milli>(parseEnd - parseStart).count();
                if (!ok)
                {
                    std::cerr << "  " << path.toStdString() << " | parse failed: " << err << std::endl;
                    continue;
                }
            }

            const auto constructStart = std::chrono::steady_clock::now();
            auto model = std::make_unique<Model>(path.toStdString(), &engine);
            const auto constructEnd = std::chrono::steady_clock::now();
            constructMs = std::chrono::duration<double, std::milli>(constructEnd - constructStart).count();

            const auto scaleStart = std::chrono::steady_clock::now();
            model->resizeToUnitBox();
            const auto scaleEnd = std::chrono::steady_clock::now();
            scaleMs = std::chrono::duration<double, std::milli>(scaleEnd - scaleStart).count();

            const double elapsedMs = parseMs + constructMs + scaleMs;
            totalMs += elapsedMs;

            std::cout << "  " << path.toStdString()
                      << " | meshes=" << model->meshes.size()
                      << " | parse_ms=" << parseMs
                      << " | construct_ms=" << constructMs
                      << " | scale_ms=" << scaleMs
                      << " | load_ms=" << elapsedMs
                      << std::endl;
        }

        std::cout << "Total load_ms=" << totalMs << std::endl;
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "asset_load_profile failed: " << ex.what() << std::endl;
        return 1;
    }
}
