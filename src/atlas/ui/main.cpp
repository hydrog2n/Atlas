#include "atlas/domain/project.hpp"
#include "atlas/ui/main_window.hpp"

#include <QApplication>

// Desktop application entry point.
int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    const auto project = atlas::domain::Project::empty(
        "00000000-0000-4000-8000-000000000001",
        "00000000-0000-4000-8000-000000000002");
    atlas::ui::MainWindow window(project);
    window.show();

    return application.exec();
}
