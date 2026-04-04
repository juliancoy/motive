#include "main_window_shell.h"
#include "asset_browser_widget.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>

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
    spinBox->setRange(min, max);
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

    static const QStringList convertibleSuffixes = {
        QStringLiteral("obj"),
        QStringLiteral("dae"),
        QStringLiteral("3ds"),
        QStringLiteral("blend")
    };

    QDir dir(absoluteRoot);
    if (!dir.exists())
    {
        return;
    }

    const QList<QFileInfo> missingEntries = findConvertibleSourcesWithoutGltf(absoluteRoot, convertibleSuffixes);
    QStringList missingConversions;
    for (const QFileInfo& entry : missingEntries)
    {
        missingConversions.push_back(dir.relativeFilePath(entry.absoluteFilePath()));
    }

    if (missingConversions.isEmpty())
    {
        m_promptedConversionRoots.push_back(absoluteRoot);
        return;
    }

    QString commandPreview;
    for (const QFileInfo& entry : missingEntries)
    {
        const auto command = conversionCommandForFile(entry);
        if (command.has_value())
        {
            commandPreview = formatCommandLine(*command);
            break;
        }
    }
    if (commandPreview.isEmpty())
    {
        commandPreview = missingConverterMessageForFile(missingEntries.front());
    }

    QMessageBox prompt(this);
    prompt.setIcon(QMessageBox::Question);
    prompt.setWindowTitle(QStringLiteral("Convert To GLTF"));
    prompt.setText(QStringLiteral("Convert source assets in this folder tree to GLTF?"));
    prompt.setInformativeText(
        QStringLiteral("The following files do not have a matching .gltf file:\n%1\n\nConverter command:\n%2")
            .arg(missingConversions.join(QStringLiteral("\n")), commandPreview));
    prompt.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    prompt.setDefaultButton(QMessageBox::Yes);
    const int choice = prompt.exec();

    if (choice == QMessageBox::Yes)
    {
        QStringList converted;
        QStringList failed;

        for (const QFileInfo& entry : missingEntries)
        {
            QString errorMessage;
            if (convertSourceFileToGltf(entry, &errorMessage))
            {
                converted.push_back(dir.relativeFilePath(entry.absoluteFilePath()));
            }
            else
            {
                failed.push_back(QStringLiteral("%1: %2").arg(dir.relativeFilePath(entry.absoluteFilePath()), errorMessage));
            }
        }

        if (m_assetBrowser)
        {
            m_assetBrowser->setRootPath(absoluteRoot);
        }

        QMessageBox result(this);
        result.setWindowTitle(QStringLiteral("GLTF Conversion"));
        result.setIcon(failed.isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
        result.setText(failed.isEmpty()
                           ? QStringLiteral("Conversion completed.")
                           : QStringLiteral("Conversion finished with some failures."));
        QString details;
        if (!converted.isEmpty())
        {
            details += QStringLiteral("Converted:\n%1").arg(converted.join(QStringLiteral("\n")));
        }
        if (!failed.isEmpty())
        {
            if (!details.isEmpty())
            {
                details += QStringLiteral("\n\n");
            }
            details += QStringLiteral("Failed:\n%1").arg(failed.join(QStringLiteral("\n")));
        }
        result.setInformativeText(details.isEmpty() ? QStringLiteral("No files needed conversion.") : details);
        result.exec();
    }

    if (m_promptedConversionRoots.indexOf(absoluteRoot) == -1)
    {
        m_promptedConversionRoots.push_back(absoluteRoot);
    }
}

}  // namespace motive::ui
