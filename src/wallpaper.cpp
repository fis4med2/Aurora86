#include "wallpaper.h"
#include "system.h"
#include <QImageReader>
#include <QImageWriter>
#include <QtWidgets>

namespace {
constexpr qint64 MaxFileBytes = 64 * 1024 * 1024;
const QString ImportedPath = "/home/guest/images/papel-de-parede.png";

QImage decode(QIODevice *device, QString *error = nullptr) {
    QImageReader reader(device);
    reader.setAutoTransform(true);
    const QByteArray format = reader.format().toLower();
    const QList<QByteArray> allowed = {"png", "jpeg", "jpg", "bmp", "ppm", "pbm", "pgm", "webp"};
    const QSize size = reader.size();
    if (!allowed.contains(format) || !size.isValid()) {
        if (error)
            *error = "Formato inválido. Escolha uma imagem PNG, JPEG, BMP, PPM ou WebP.";
        return {};
    }
    if (size.width() > 32768 || size.height() > 32768 || qint64(size.width()) * size.height() > 64000000) {
        if (error)
            *error = "Imagem grande demais. Reduza a resolução antes de importar.";
        return {};
    }
    if (size.width() > 960 || size.height() > 640)
        reader.setScaledSize(size.scaled(960, 640, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull()) {
        if (error)
            *error = "Não foi possível ler a imagem. Ela pode estar danificada ou ultrapassar o limite de "
                     "memória.";
        return {};
    }
    if (image.width() > 960 || image.height() > 640)
        image = image.scaled(960, 640, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}
QImage virtualImage(SimSystem *system, const QString &path, QString *error = nullptr) {
    QByteArray data = system->read(system->normalize(path));
    const QString readError = system->lastError();
    if (!readError.isEmpty()) {
        if (error)
            *error = readError;
        return {};
    }
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    return decode(&buffer, error);
}
QImage retroImage(QImage image) {
    image = image.convertToFormat(QImage::Format_ARGB32);
    const QRgb palette[] = {qRgb(23, 43, 42), qRgb(84, 115, 97), qRgb(182, 204, 149), qRgb(229, 230, 200)};
    for (int y = 0; y < image.height(); ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb old = pixels[x];
            pixels[x] = (palette[qGray(old) / 64] & 0x00ffffff) | (quint32(qAlpha(old)) << 24);
        }
    }
    return image;
}
QPixmap preview(const QImage &original, QSize size, const QString &mode, bool retro) {
    QImage image = retro ? retroImage(original) : original;
    QPixmap output(size);
    output.fill(QColor("#203d35"));
    QPainter painter(&output);
    const QSize target = mode == "stretch" ? size
                         : mode == "center"
                             ? image.size()
                             : image.size().scaled(size, mode == "cover" ? Qt::KeepAspectRatioByExpanding
                                                                         : Qt::KeepAspectRatio);
    const QImage scaled = image.scaled(target, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    painter.drawImage((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2, scaled);
    return output;
}
bool imageExtension(const QString &path) {
    return QStringList{"png", "jpg", "jpeg", "bmp", "ppm", "pbm", "pgm", "webp"}.contains(
        path.section('.', -1).toLower());
}
} // namespace

bool importWallpaper(SimSystem *system, const QString &localPath, QString *error) {
    if (error)
        error->clear();
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!QFileInfo(localPath).isFile())
        return fail("Escolha um arquivo de imagem comum.");
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly))
        return fail("Não foi possível abrir a imagem escolhida.");
    if (file.size() > MaxFileBytes)
        return fail("Arquivo grande demais. O limite de importação é 64 MiB.");
    QString decodeError;
    const QImage image = decode(&file, &decodeError);
    if (image.isNull())
        return fail(decodeError);
    QByteArray png;
    QBuffer encoded(&png);
    encoded.open(QIODevice::WriteOnly);
    QImageWriter writer(&encoded, "png");
    writer.setCompression(9);
    if (!writer.write(image))
        return fail("Não foi possível preparar a cópia da imagem.");

    const QString previousPath = system->setting("wallpaperPath", "").toString();
    // Persist the new setting before touching an existing image. Failed writes
    // restore the previous setting and SimSystem keeps the previous file intact.
    if (previousPath != ImportedPath) {
        system->setSetting("wallpaperPath", ImportedPath);
        if (system->setting("wallpaperPath").toString() != ImportedPath)
            return fail(system->lastError().isEmpty() ? "Não foi possível gravar a preferência."
                                                      : system->lastError());
    }
    if (!system->write(ImportedPath, png)) {
        const QString writeError = system->lastError();
        if (previousPath != ImportedPath)
            system->setSetting("wallpaperPath", previousPath);
        return fail(writeError);
    }
    return true;
}
QImage applyWallpaperRetro(QImage image) { return retroImage(std::move(image)); }
QImage readWallpaper(SimSystem *system) {
    const QString path = system->setting("wallpaperPath", "").toString();
    return path.isEmpty() ? QImage() : virtualImage(system, path);
}

QWidget *createWallpaperPanel(SimSystem *system, QWidget *parent) {
    auto *widget = new QWidget(parent);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    auto *intro = new QLabel(
        "Use sua própria imagem como fundo. A importação copia uma versão reduzida para o disco virtual.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *importButton = new QPushButton("Importar imagem do Linux...");
    importButton->setObjectName("wallpaperImport");
    auto *importRow = new QHBoxLayout;
    importRow->addWidget(importButton, 1);
    layout->addLayout(importRow);
    auto *selectRow = new QHBoxLayout;
    auto *files = new QComboBox;
    files->setObjectName("wallpaperVirtual");
    files->setEditable(true);
    files->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    files->setInsertPolicy(QComboBox::NoInsert);
    files->lineEdit()->setPlaceholderText("/home/guest/images/minha-imagem.png");
    selectRow->addWidget(files, 1);
    auto *apply = new QPushButton("Aplicar imagem");
    apply->setObjectName("wallpaperApply");
    selectRow->addWidget(apply);
    layout->addLayout(selectRow);
    auto *frame = new QLabel;
    frame->setObjectName("wallpaperPreview");
    frame->setAlignment(Qt::AlignCenter);
    frame->setMinimumSize(360, 120);
    frame->setMaximumHeight(180);
    frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    frame->setStyleSheet("border:1px solid palette(text);background:#203d35;color:#e5e6c8;padding:4px");
    layout->addWidget(frame, 1);
    auto *form = new QFormLayout;
    auto *mode = new QComboBox;
    mode->setObjectName("wallpaperMode");
    mode->addItem("Preencher a tela (recorta bordas)", "cover");
    mode->addItem("Mostrar inteira (com bordas)", "fit");
    mode->addItem("Esticar até as bordas", "stretch");
    mode->addItem("Centralizar sem ampliar", "center");
    form->addRow("Encaixe", mode);
    auto *retro = new QCheckBox("Converter para quatro tons retrô");
    retro->setObjectName("wallpaperRetro");
    form->addRow("Cores", retro);
    layout->addLayout(form);
    auto *restore = new QPushButton("Restaurar fundo original");
    restore->setObjectName("wallpaperRestore");
    importRow->addWidget(restore);
    auto *status = new QLabel;
    status->setWordWrap(true);
    layout->addWidget(status);
    const auto showPreview = [system, files, frame, mode, retro, status] {
        const QString path = files->currentText().trimmed();
        if (path.isEmpty()) {
            frame->setPixmap({});
            frame->setText("AURORA / 86\nFundo original");
            status->setText("Selecione uma imagem existente ou importe uma imagem do Linux.");
            return;
        }
        QString error;
        const QImage image = virtualImage(system, path, &error);
        if (image.isNull()) {
            frame->setPixmap({});
            frame->setText("Prévia indisponível");
            status->setText(error);
            return;
        }
        frame->setPixmap(preview(image, QSize(420, 140), mode->currentData().toString(), retro->isChecked()));
        status->setText(QString("%1  •  %2 × %3 pixels").arg(path).arg(image.width()).arg(image.height()));
    };
    const auto refresh = [system, files, mode, retro, showPreview] {
        const QSignalBlocker stopFiles(files), stopMode(mode), stopRetro(retro);
        const QString current = system->setting("wallpaperPath", "").toString();
        files->clear();
        files->addItem("");
        for (const VEntry &entry : system->list("/home/guest/images"))
            if (!entry.directory && imageExtension(entry.path))
                files->addItem(entry.path);
        if (!current.isEmpty() && files->findText(current) < 0)
            files->addItem(current);
        files->setCurrentText(current);
        mode->setCurrentIndex(qMax(0, mode->findData(system->setting("wallpaperMode", "cover"))));
        retro->setChecked(system->setting("wallpaperRetro", false).toBool());
        showPreview();
    };
    QObject::connect(importButton, &QPushButton::clicked, widget, [widget, system, status, refresh] {
        const QString filename = QFileDialog::getOpenFileName(
            widget, "Escolher papel de parede", {},
            "Imagens (*.png *.jpg *.jpeg *.bmp *.ppm *.pbm *.pgm *.webp);;Todos os arquivos (*)");
        if (filename.isEmpty())
            return;
        QString error;
        if (!importWallpaper(system, filename, &error)) {
            QMessageBox::warning(widget, "Importar papel de parede", error);
            return;
        }
        refresh();
        status->setText("Imagem copiada e aplicada. O arquivo original no Linux foi preservado.");
    });
    QObject::connect(apply, &QPushButton::clicked, widget, [widget, system, files] {
        const QString path = system->normalize(files->currentText().trimmed());
        QString error;
        if (virtualImage(system, path, &error).isNull()) {
            QMessageBox::warning(widget, "Aplicar papel de parede", error);
            return;
        }
        system->setSetting("wallpaperPath", path);
        if (system->setting("wallpaperPath").toString() != path)
            QMessageBox::warning(widget, "Aplicar papel de parede", system->lastError());
    });
    QObject::connect(restore, &QPushButton::clicked, widget, [widget, system] {
        system->setSetting("wallpaperPath", "");
        if (!system->setting("wallpaperPath").toString().isEmpty())
            QMessageBox::warning(widget, "Restaurar fundo", system->lastError());
    });
    QObject::connect(mode, &QComboBox::currentIndexChanged, widget, [widget, system, mode, showPreview] {
        system->setSetting("wallpaperMode", mode->currentData());
        const bool saved = system->setting("wallpaperMode") == mode->currentData();
        const QString error = system->lastError();
        showPreview();
        if (!saved)
            QMessageBox::warning(widget, "Encaixe", error);
    });
    QObject::connect(retro, &QCheckBox::toggled, widget, [widget, system, showPreview](bool checked) {
        system->setSetting("wallpaperRetro", checked);
        const bool saved = system->setting("wallpaperRetro").toBool() == checked;
        const QString error = system->lastError();
        showPreview();
        if (!saved)
            QMessageBox::warning(widget, "Cores", error);
    });
    QObject::connect(files, &QComboBox::editTextChanged, widget, showPreview);
    QObject::connect(system, &SimSystem::changed, widget, refresh);
    QObject::connect(system, &SimSystem::settingsChanged, widget, refresh);
    refresh();
    return widget;
}
