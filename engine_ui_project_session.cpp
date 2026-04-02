#include "engine_ui_project_session.h"

namespace motive::ui {

ProjectSession::ProjectSession()
{
}

ProjectSession::~ProjectSession() = default;

QString ProjectSession::currentProjectId() const
{
    return m_currentProjectId;
}

QString ProjectSession::currentProjectRoot() const
{
    return m_currentProjectRoot;
}

}  // namespace motive::ui
