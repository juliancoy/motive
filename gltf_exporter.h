#pragma once

#include <QString>

namespace motive::exporter {

bool exportFbxAssetToGltf(const QString& sourceFbxPath,
                          const QString& outputPath,
                          QString* errorMessage);

}
