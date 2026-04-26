#pragma once

#include "control_server.h"

#include <QString>

namespace motive::ui {

class MainWindowShell;

// Adapter service that captures editor profile/root data for control-server endpoints.
// This is intentionally thin and side-effect free: it only snapshots current state.
class ProfileDataService
{
public:
    explicit ProfileDataService(MainWindowShell& window);

    QString resolveRootPath() const;
    EngineUiControlServer::ProfileData captureProfileData() const;

private:
    MainWindowShell& m_window;
};

}  // namespace motive::ui

