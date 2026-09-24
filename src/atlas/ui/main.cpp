#include "atlas/domain/project.hpp"
#include "atlas/ui/canvas_widget.hpp"

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMainWindow>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>

// Desktop application entry point.
int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    QMainWindow window;
    window.setWindowTitle("Atlas");
    window.resize(1280, 800);

    // Build a minimal project object from the domain layer.
    const auto project = atlas::domain::Project::empty(
        "00000000-0000-4000-8000-000000000001",
        "00000000-0000-4000-8000-000000000002");

    // The canvas owns only derived camera and presentation state; source records remain elsewhere.
    auto* canvas = new atlas::ui::CanvasWidget;
    canvas->setProject(project);
    canvas->setAccessibleName(QStringLiteral("Atlas canvas for project %1")
                                  .arg(QString::fromStdString(project.id())));
    window.setCentralWidget(canvas);

    auto* hierarchyDock = new QDockWidget(QStringLiteral("Hierarchy"), &window);
    hierarchyDock->setObjectName(QStringLiteral("hierarchyDock"));
    auto* hierarchy = new QTreeWidget(hierarchyDock);
    hierarchy->setHeaderLabel(QStringLiteral("Map objects"));
    hierarchy->setAccessibleName(QStringLiteral("Map hierarchy"));
    auto* projectItem = new QTreeWidgetItem(hierarchy, {QStringLiteral("Project")});
    projectItem->setData(0, Qt::UserRole, QString::fromStdString(project.id()));
    auto* mapItem = new QTreeWidgetItem(projectItem, {QStringLiteral("Map: %1")
                                                          .arg(QString::fromStdString(project.rootMap().id))});
    mapItem->setData(0, Qt::UserRole, QString::fromStdString(project.rootMap().id));
    hierarchy->expandAll();
    hierarchyDock->setWidget(hierarchy);
    window.addDockWidget(Qt::LeftDockWidgetArea, hierarchyDock);

    auto* inspectorDock = new QDockWidget(QStringLiteral("Inspector"), &window);
    inspectorDock->setObjectName(QStringLiteral("inspectorDock"));
    auto* inspector = new QLabel(QStringLiteral("No object selected"), inspectorDock);
    inspector->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    inspector->setAccessibleName(QStringLiteral("Object inspector"));
    inspectorDock->setWidget(inspector);
    window.addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    window.statusBar()->showMessage(QStringLiteral("Coordinates: 0.000 m | Level: Ground | Layer: Default"));

    auto* commandPalette = new QMenu(QStringLiteral("Command Palette"), &window);
    auto* focusCanvas = commandPalette->addAction(QStringLiteral("Focus canvas"));
    focusCanvas->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    focusCanvas->setShortcutVisibleInContextMenu(true);
    QObject::connect(focusCanvas, &QAction::triggered, canvas, [canvas]() {
        canvas->setFocus(Qt::ShortcutFocusReason);
    });
    window.menuBar()->addMenu(commandPalette);
    window.show();

    return application.exec();
}
