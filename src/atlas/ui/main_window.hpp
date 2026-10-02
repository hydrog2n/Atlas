#pragma once

#include "atlas/application/command.hpp"
#include "atlas/ui/canvas_widget.hpp"

#include <QMainWindow>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QComboBox;
class QCheckBox;
class QDockWidget;
class QLabel;
class QTreeWidget;
class QDoubleSpinBox;
class QPushButton;
class QWidget;
class QToolBar;

namespace atlas::ui {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(domain::Project project, QWidget* parent = nullptr);

    const application::CommandProcessor& commandProcessor() const noexcept;
    CanvasWidget* canvas() const noexcept;
    bool openPackage(const std::filesystem::path& packagePath, std::string* error = nullptr);
    bool savePackage(
        const std::filesystem::path& packagePath,
        bool allowSchemaMigration = false,
        std::string* error = nullptr);

private:
    void refreshProjectViews();
    void updateHistoryActions();
    void previewCommand(std::unique_ptr<application::Command> command);
    void applyPreview();
    void cancelPreview();
    void selectRoad(const std::string& roadId);
    void updateInspector();
    void previewControlPointEdit();
    void splitAtStation(const std::string& roadId, double station);
    void reverseSelectedRoad();
    void deleteSelectedRoad();
    void extendSelectedRoad();
    void shortenSelectedRoad();
    void mergeSelectedSegments();
    void moveSelectedBoundary();
    bool confirmDiscardChanges();
    void newProject();
    void openFromDialog();
    void saveFromDialog();
    void saveAsFromDialog();
    bool isDirty() const;

    application::CommandProcessor commandProcessor_;
    CanvasWidget* canvas_ = nullptr;
    QTreeWidget* hierarchy_ = nullptr;
    QLabel* inspector_ = nullptr;
    QWidget* inspectorPanel_ = nullptr;
    QDockWidget* changesDock_ = nullptr;
    QLabel* previewSummary_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QComboBox* primitiveMode_ = nullptr;
    QCheckBox* gridSnapToggle_ = nullptr;
    QDoubleSpinBox* radiusEditor_ = nullptr;
    QDoubleSpinBox* widthEditor_ = nullptr;
    QComboBox* controlPointSelector_ = nullptr;
    QDoubleSpinBox* controlPointX_ = nullptr;
    QDoubleSpinBox* controlPointY_ = nullptr;
    QDoubleSpinBox* endpointX_ = nullptr;
    QDoubleSpinBox* endpointY_ = nullptr;
    QDoubleSpinBox* measureStart_ = nullptr;
    QDoubleSpinBox* measureEnd_ = nullptr;
    QDoubleSpinBox* boundaryStation_ = nullptr;
    QPushButton* measureButton_ = nullptr;
    QToolBar* roadContextToolbar_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    std::unique_ptr<application::Command> pendingCommand_;
    std::optional<application::Preview> pendingPreview_;
    std::string selectedRoadId_;
    std::string selectedSegmentId_;
    std::string pendingSelectionId_;
    std::vector<std::string> undoSelection_;
    std::vector<std::string> redoSelection_;
    std::filesystem::path packagePath_;
    std::string savedSource_;
    int schemaGeneration_ = 2;
};

} // namespace atlas::ui