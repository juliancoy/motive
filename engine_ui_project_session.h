#pragma once

#include <QString>

namespace motive::ui {

class ProjectSession
{
public:
    ProjectSession();
    ~ProjectSession();

    QString currentProjectId() const;
    QString currentProjectRoot() const;

private:
    QString m_currentProjectId;
    QString m_currentProjectRoot;
};

}  // namespace motive::ui
