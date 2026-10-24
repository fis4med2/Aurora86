#include "display.h"
#include "system.h"
#include <QtWidgets>
QSize displayPresetSize(const QString &preset) {
    static const QMap<QString, QSize> presets = {{"960x640", {960, 640}},   {"1024x768", {1024, 768}},
                                                 {"1152x864", {1152, 864}}, {"1280x800", {1280, 800}},
                                                 {"1280x960", {1280, 960}}, {"1440x900", {1440, 900}},
                                                 {"1600x900", {1600, 900}}};
    return presets.value(preset);
}
QWidget *createDisplayPanel(SimSystem *system, QWidget *parent) {
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *panel = new QWidget;
    scroll->setWidget(panel);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);
    auto *intro = new QLabel("LUME DISPLAY ADAPTER / virtual monitor");
    layout->addWidget(intro);
    auto *description =
        new QLabel("Choose the virtual desktop resolution. Auto follows the window. Fixed modes keep their "
                   "exact pixel size; use the scrollbars if the monitor is larger than the window.");
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    auto *preset = new QComboBox;
    preset->setObjectName("displayPreset");
    preset->addItem("Auto / fit the window", "auto");
    for (const auto &id :
         QStringList{"960x640", "1024x768", "1152x864", "1280x800", "1280x960", "1440x900", "1600x900"})
        preset->addItem(QString(id).replace("x", " × "), id);
    preset->setCurrentIndex(qMax(0, preset->findData(system->setting("displayPreset", "auto"))));
    form->addRow("Resolution", preset);
    auto *actual = new QLabel;
    actual->setObjectName("displayActual");
    form->addRow("Current desktop", actual);
    auto *full = new QCheckBox("Start the simulator in fullscreen");
    full->setObjectName("startupFullscreen");
    full->setChecked(system->setting("startupFullscreen", false).toBool());
    form->addRow("Startup", full);
    auto *apply = new QPushButton("Apply resolution");
    apply->setObjectName("displayApply");
    layout->addWidget(apply);
    auto *status = new QLabel;
    status->setObjectName("displayStatus");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto refresh = [system, actual] {
        actual->setText(QString("%1 × %2 pixels")
                            .arg(system->setting("resolutionWidth", 960).toInt())
                            .arg(system->setting("resolutionHeight", 640).toInt()));
    };
    QObject::connect(system, &SimSystem::settingsChanged, panel, refresh);
    refresh();
    QObject::connect(apply, &QPushButton::clicked, panel, [system, preset, status] {
        QString choice = preset->currentData().toString();
        system->setSetting("displayPreset", choice);
        status->setText(system->setting("displayPreset", "auto").toString() == choice
                            ? "Display mode applied and saved."
                            : "Could not save this display mode.");
    });
    QObject::connect(full, &QCheckBox::toggled, panel, [system, full](bool enabled) {
        system->setSetting("startupFullscreen", enabled);
        if (system->setting("startupFullscreen", false).toBool() != enabled) {
            QSignalBlocker block(full);
            full->setChecked(!enabled);
        }
    });
    auto *note = new QLabel("F11 toggles fullscreen at any time. These controls only change the simulated "
                            "display; Linux monitor settings stay untouched.");
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();
    return scroll;
}
