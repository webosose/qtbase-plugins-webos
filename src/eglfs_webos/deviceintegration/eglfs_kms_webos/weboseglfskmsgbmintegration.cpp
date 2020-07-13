// Copyright (c) 2020 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>

#include "weboseglfskmsgbmintegration.h"

WebOSKmsScreenConfig::WebOSKmsScreenConfig(QJsonObject config)
    : QKmsScreenConfig()
    , m_configJson(config)
{
}

void WebOSKmsScreenConfig::loadConfig()
{
    if (m_configJson.isEmpty()) {
        qWarning() << "No config set";
        return;
    }

    m_hwCursor = m_configJson.value(QLatin1String("hwcursor")).toBool(m_hwCursor);
    m_devicePath = m_configJson.value(QLatin1String("device")).toString();

    const QJsonArray outputs = m_configJson.value(QLatin1String("outputs")).toArray();
    m_outputSettings.clear();
    for (int i = 0; i < outputs.size(); i++) {
        const QVariantMap outputSettings = outputs.at(i).toObject().toVariantMap();
        if (outputSettings.contains(QStringLiteral("name"))) {
            const QString name = outputSettings.value(QStringLiteral("name")).toString();
            if (m_outputSettings.contains(name))
                qWarning() << "Output" << name << "is duplicated";
            m_outputSettings.insert(name, outputSettings);
        }
    }
}

WebOSEglFSKmsGbmIntegration::WebOSEglFSKmsGbmIntegration()
    : QEglFSKmsGbmIntegration()
{
    static QByteArray json = qgetenv("QT_QPA_EGLFS_CONFIG");

    if (!json.isEmpty()) {
        QFile file(QString::fromUtf8(json));
        if (file.open(QFile::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                m_configJson = doc.array().at(0).toObject();
                qInfo() << "Using config file" << json;
            } else {
                qWarning() << "Invalid config file" << json << "- no top-level JSON object";
            }
            file.close();
        } else {
            qWarning() << "Could not open config file" << json << "for reading";
        }
    } else {
        qWarning("No config file given");
    }
}

QKmsScreenConfig *WebOSEglFSKmsGbmIntegration::createScreenConfig()
{
    QKmsScreenConfig *screenConfig = new WebOSKmsScreenConfig(m_configJson);
    screenConfig->loadConfig();

    return screenConfig;
}
