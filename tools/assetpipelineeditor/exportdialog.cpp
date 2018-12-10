/*
    exportdialog.cpp

    This file is part of Kuesa.

    Copyright (C) 2019 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
    Author: Jean-Michaël Celerier <jean-michael.celerier@kdab.com>

    Licensees holding valid proprietary KDAB Kuesa licenses may use this file in
    accordance with the Kuesa Enterprise License Agreement provided with the Software in the
    LICENSE.KUESA.ENTERPRISE file.

    Contact info@kdab.com if any conditions of this licensing are not clear to you.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "exportdialog.h"
#include "ui_exportdialog.h"

#include <Kuesa/private/gltf2exporter_p.h>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QFileDialog>
#include <QStyle>
#include <QPushButton>
#include <QMessageBox>

ExportDialog::ExportDialog(Kuesa::GLTF2Exporter &exporter, const QString &path, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ExportDialog)
    , m_exporter(exporter)
    , m_originalFile(path)
    , m_targetFile(m_originalFile)
{
    ui->setupUi(this);

    ui->sourceLabel->setText(tr("Saving: ") + m_originalFile);

    auto save_btn = ui->buttonBox->button(QDialogButtonBox::Save);
    connect(save_btn, &QPushButton::clicked,
            this, &ExportDialog::onSave);
}

ExportDialog::~ExportDialog()
{
    delete ui;
}

void ExportDialog::onSave()
{
    ui->errorLog->setPlainText("");

    m_targetFile = QFileDialog::getSaveFileName(this, tr("Select a folder to save to"), m_targetFile, tr("glTF 2.0 file (*.gltf)"));
    if (m_targetFile.isNull())
        return;

    const QDir orig_dir = QFileInfo(m_originalFile).dir();
    const QDir target_dir = QFileInfo(m_targetFile).dir();
    if (orig_dir.absolutePath() == target_dir.absolutePath()) {
        const int res = QMessageBox::warning(
                this, tr("Destructive operation"),
                tr("Saving in the original folder will overwrite assets."),
                QMessageBox::Ok | QMessageBox::Cancel,
                QMessageBox::Cancel);
        if (res != QMessageBox::Ok) {
            ui->errorLog->setPlainText(tr("Nothing saved."));
            return;
        }
    }

    // Set-up configuration
    {
        using ExportConf = Kuesa::GLTF2ExportConfiguration;
        ExportConf conf;

        conf.setMeshCompressionEnabled(ui->meshCompression->isChecked());

        conf.setMeshEncodingSpeed(ui->encodeSpeedSlider->value());
        conf.setMeshDecodingSpeed(ui->decodeSpeedSlider->value());

        // The UI sliders are between 0 - 16.
        // 0 is an invalid compression value for draco,
        // so we shift everything by one and put 0 to the max position
        auto mapSlider = [](QSlider *s) {
            const auto max = s->maximum();
            const auto v = s->value();
            if (v == max)
                return 0;
            else
                return v + 1;
        };
        conf.setAttributeQuantizationLevel(ExportConf::Position, mapSlider(ui->positionQuantizationSlider));
        conf.setAttributeQuantizationLevel(ExportConf::Normal, mapSlider(ui->normalQuantizationSlider));
        conf.setAttributeQuantizationLevel(ExportConf::Color, mapSlider(ui->colorQuantizationSlider));
        conf.setAttributeQuantizationLevel(ExportConf::TextureCoordinate, mapSlider(ui->textureQuantizationSlider));
        conf.setAttributeQuantizationLevel(ExportConf::Generic, mapSlider(ui->genericQuantizationSlider));

        m_exporter.setConfiguration(conf);
    }

    const auto rootObject = m_exporter.saveInFolder(orig_dir, target_dir);

    for (const auto &error : m_exporter.errors()) {
        ui->errorLog->appendPlainText(error);
        ui->errorLog->appendPlainText(QStringLiteral("\n"));
    }
    if (rootObject.empty()) {
        return;
    }

    QFile outFile(m_targetFile);
    if (outFile.open(QIODevice::WriteOnly)) {
        outFile.write(QJsonDocument(rootObject).toJson());

        ui->errorLog->appendPlainText(tr("Success"));
    } else {
        ui->errorLog->appendPlainText(tr("Cannot write to output file"));
    }
}
