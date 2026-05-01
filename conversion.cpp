#include "shell.h"
#include "asset_browser_widget.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>

#include <limits>
#include <optional>

namespace motive::ui {

namespace {

struct ConversionCommand
{
    QString program;
    QStringList arguments;
};

QString formatCommandLine(const ConversionCommand& command)
{
    QStringList parts;
    parts.push_back(command.program);
    parts.append(command.arguments);

    for (QString& part : parts)
    {
        if (part.contains(' '))
        {
            part = QStringLiteral("\"%1\"").arg(part);
        }
    }

    return parts.join(' ');
}

QString replacePlaceholder(QString value, const QString& inputPath, const QString& outputPath)
{
    value.replace(QStringLiteral("{input}"), inputPath);
    value.replace(QStringLiteral("{output}"), outputPath);
    return value;
}

bool isFbxFile(const QFileInfo& inputFile)
{
    return inputFile.suffix().compare(QStringLiteral("fbx"), Qt::CaseInsensitive) == 0;
}

QString suggestedConverterInstallText()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral(
        "Suggested tools:\n"
        "- brew install assimp\n"
        "- or install FBX2glTF / fbx2gltf separately\n"
        "- or set MOTIVE_GLTF_CONVERTER='your-command {input} {output}'");
#elif defined(Q_OS_WIN)
    return QStringLiteral(
        "Suggested tools:\n"
        "- install FBX2glTF or fbx2gltf and add it to PATH\n"
        "- or install assimp and use `assimp export`\n"
        "- or set MOTIVE_GLTF_CONVERTER=\"your-command {input} {output}\"");
#elif defined(Q_OS_LINUX)
    QString distroId;
    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (osRelease.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QString text = QString::fromUtf8(osRelease.readAll());
        const QStringList lines = text.split('\n');
        for (const QString& line : lines)
        {
            if (line.startsWith(QStringLiteral("ID=")))
            {
                distroId = line.mid(3).trimmed().remove('"').toLower();
                break;
            }
        }
    }

    if (distroId == QStringLiteral("ubuntu") || distroId == QStringLiteral("debian"))
    {
        return QStringLiteral(
            "Suggested tools:\n"
            "- sudo apt install assimp-utils\n"
            "- or install FBX2glTF / fbx2gltf separately\n"
            "- or set MOTIVE_GLTF_CONVERTER='your-command {input} {output}'");
    }
    if (distroId == QStringLiteral("fedora"))
    {
        return QStringLiteral(
            "Suggested tools:\n"
            "- sudo dnf install assimp\n"
            "- or install FBX2glTF / fbx2gltf separately\n"
            "- or set MOTIVE_GLTF_CONVERTER='your-command {input} {output}'");
    }
    if (distroId == QStringLiteral("arch"))
    {
        return QStringLiteral(
            "Suggested tools:\n"
            "- sudo pacman -S assimp\n"
            "- or install FBX2glTF / fbx2gltf separately\n"
            "- or set MOTIVE_GLTF_CONVERTER='your-command {input} {output}'");
    }

    return QStringLiteral(
        "Suggested tools:\n"
        "- install assimp, FBX2glTF, or fbx2gltf\n"
        "- or set MOTIVE_GLTF_CONVERTER='your-command {input} {output}'");
#else
    return QStringLiteral(
        "Suggested tools:\n"
        "- install assimp, FBX2glTF, or fbx2gltf\n"
        "- or set MOTIVE_GLTF_CONVERTER='your-command {input} {output}'");
#endif
}

std::optional<ConversionCommand> conversionCommandForFile(const QFileInfo& inputFile)
{
    const QString inputPath = inputFile.absoluteFilePath();
    const QString outputPath = inputFile.absolutePath() + QDir::separator() +
                               inputFile.completeBaseName() + QStringLiteral(".gltf");
    const bool isFbx = isFbxFile(inputFile);

    const QString overrideCommand = qEnvironmentVariable("MOTIVE_GLTF_CONVERTER");
    if (!overrideCommand.trimmed().isEmpty())
    {
        QStringList parts = QProcess::splitCommand(overrideCommand);
        if (!parts.isEmpty())
        {
            ConversionCommand command;
            command.program = replacePlaceholder(parts.takeFirst(), inputPath, outputPath);
            for (QString& part : parts)
            {
                command.arguments.push_back(replacePlaceholder(part, inputPath, outputPath));
            }
            return command;
        }
    }

    const QString fbx2gltf = QStandardPaths::findExecutable(QStringLiteral("fbx2gltf"));
    if (isFbx && !fbx2gltf.isEmpty())
    {
        return ConversionCommand{fbx2gltf, {QStringLiteral("-i"), inputPath, QStringLiteral("-o"), outputPath}};
    }

    const QString FBX2glTF = QStandardPaths::findExecutable(QStringLiteral("FBX2glTF"));
    if (isFbx && !FBX2glTF.isEmpty())
    {
        return ConversionCommand{FBX2glTF, {QStringLiteral("-i"), inputPath, QStringLiteral("-o"), outputPath}};
    }

    if (isFbx)
    {
        return std::nullopt;
    }

    const QString assimp = QStandardPaths::findExecutable(QStringLiteral("assimp"));
    if (!assimp.isEmpty())
    {
        return ConversionCommand{assimp, {QStringLiteral("export"), inputPath, outputPath}};
    }

    return std::nullopt;
}

QString missingConverterMessageForFile(const QFileInfo& inputFile)
{
    if (isFbxFile(inputFile))
    {
        return QStringLiteral(
            "No animation-safe FBX converter detected.\n"
            "FBX files are not routed through `assimp export` because animation, skinning, or rig data may be lost.\n\n%1")
            .arg(suggestedConverterInstallText());
    }

    return QStringLiteral(
        "No converter found.\n%1").arg(suggestedConverterInstallText());
}

bool convertSourceFileToGltf(const QFileInfo& inputFile, QString* errorMessage)
{
    const auto command = conversionCommandForFile(inputFile);
    if (!command.has_value())
    {
        if (errorMessage)
        {
            *errorMessage = missingConverterMessageForFile(inputFile);
        }
        return false;
    }

    QProcess process;
    process.start(command->program, command->arguments);
    if (!process.waitForStarted())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to start converter: %1").arg(command->program);
        }
        return false;
    }

    process.closeWriteChannel();
    if (!process.waitForFinished(-1))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Converter timed out for %1").arg(inputFile.fileName());
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        if (errorMessage)
        {
            const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
            const QString stdoutText = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
            *errorMessage = !stderrText.isEmpty() ? stderrText : (!stdoutText.isEmpty() ? stdoutText : QStringLiteral("converter failed"));
        }
        return false;
    }

    const QString expectedGltf = inputFile.absolutePath() + QDir::separator() +
                                 inputFile.completeBaseName() + QStringLiteral(".gltf");
    if (!QFileInfo::exists(expectedGltf))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Converter completed but did not create %1").arg(QFileInfo(expectedGltf).fileName());
        }
        return false;
    }

    return true;
}

QList<QFileInfo> findConvertibleSourcesWithoutGltf(const QString& rootPath, const QStringList& convertibleSuffixes)
{
    QList<QFileInfo> results;
    QDirIterator it(rootPath,
                    QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        const QFileInfo entry = it.fileInfo();
        const QString suffix = entry.suffix().toLower();
        if (!convertibleSuffixes.contains(suffix))
        {
            continue;
        }

        const QString expectedGltf = entry.absolutePath() + QDir::separator() +
                                     entry.completeBaseName() + QStringLiteral(".gltf");
        if (!QFileInfo::exists(expectedGltf))
        {
            results.push_back(entry);
        }
    }
    return results;
}

}  // namespace

QDoubleSpinBox* MainWindowShell::createSpinBox(QWidget* parent, double min, double max, double step)
{
    auto* spinBox = new QDoubleSpinBox(parent);
    constexpr double kNoLimit = std::numeric_limits<double>::max() / 1000.0;
    if (min == 0.0 && max == 0.0)
    {
        spinBox->setRange(-kNoLimit, kNoLimit);
    }
    else if (max == 0.0)
    {
        spinBox->setRange(min, kNoLimit);
    }
    else
    {
        spinBox->setRange(min, max);
    }
    spinBox->setSingleStep(step);
    spinBox->setDecimals(3);
    spinBox->setFixedWidth(80);
    return spinBox;
}

void MainWindowShell::maybePromptForGltfConversion(const QString& rootPath)
{
    const QString absoluteRoot = QFileInfo(rootPath).absoluteFilePath();
    if (absoluteRoot.isEmpty() || m_promptedConversionRoots.indexOf(absoluteRoot) != -1)
    {
        return;
    }
    // Disabled: startup-time modal conversion prompts can block editor launch.
    // Conversion remains available through explicit tooling/workflows.
    m_promptedConversionRoots.push_back(absoluteRoot);
}

}  // namespace motive::ui
