#pragma once

#include <QString>

namespace motive::ui {

struct AssetBrowserSelection
{
    QString filePath;
    QString directoryPath;
    QString displayName;
    bool isDirectory = false;
};

}  // namespace motive::ui
