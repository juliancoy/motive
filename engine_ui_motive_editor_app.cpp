#include "engine_ui_motive_editor_app.h"
#include "engine_ui_main_window_shell.h"

#include <QApplication>

namespace motive::ui {

int runMotiveEditorApp(int argc, char** argv)
{
    QApplication app(argc, argv);

    MainWindowShell window;
    window.show();

    return app.exec();
}

}  // namespace motive::ui
