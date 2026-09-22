#include "atlas/domain/project.hpp"

#include <QApplication>
#include <QLabel>
#include <QMainWindow>

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

    // QLabel is enough to confirm the UI can render project data without coupling to the model.
    auto* label = new QLabel(QStringLiteral("Atlas v0.1.2\nProject: %1")
                                 .arg(QString::fromStdString(project.id())));
    label->setAlignment(Qt::AlignCenter);
    window.setCentralWidget(label);
    window.show();

    return application.exec();
}
