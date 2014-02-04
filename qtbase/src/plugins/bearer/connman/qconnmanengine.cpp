/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Copyright 2013 Jolla
** Contact: http://www.qt-project.org/legal
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qconnmanengine.h"
#include "../qnetworksession_impl.h"

#include <QtNetwork/private/qnetworkconfiguration_p.h>
#include <QtNetwork/qnetworksession.h>

#include <QtCore/qdebug.h>

#include <QtDBus/QtDBus>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusReply>

#include <connman-qt5/useragent.h>
#include <connman-qt5/networkmanager.h>
#include <connman-qt5/networktechnology.h>
#include <connman-qt5/networkservice.h>

#include <qofono-qt5/qofonomodem.h>
#include <qofono-qt5/qofonoconnectionmanager.h>
#include <qofono-qt5/qofononetworkoperator.h>
#include <qofono-qt5/qofononetworkregistration.h>
#include <qofono-qt5/qofonoconnectionmanager.h>

#ifndef QT_NO_BEARERMANAGEMENT
#ifndef QT_NO_DBUS

QT_BEGIN_NAMESPACE

QConnmanEngine::QConnmanEngine(QObject *parent)
    :   QBearerEngineImpl(parent),
      netman(NetworkManagerFactory::createInstance())
{
    qDebug() << "<<<<<<<<<<<<<<<<<<<<<<<<<<<" << this;
    connect(netman,SIGNAL(servicesListChanged(QStringList)),
            this,SLOT(servicesListChanged(QStringList)));

    connmanIsAvailable = QDBusConnection::systemBus().interface()->isServiceRegistered("net.connman");
    connect(netman,SIGNAL(availabilityChanged(bool)),this,SLOT(connmanAvailabilityChanged(bool)));
}

QConnmanEngine::~QConnmanEngine()
{
}

void QConnmanEngine::connmanAvailabilityChanged(bool b)
{
    qDebug();
    connmanIsAvailable = b;
    if (b) {
        initialize();

        if (netman->technologiesList().contains("wifi")) {
            connect(netman->getTechnology("wifi"),SIGNAL(scanFinished()),
                    this,SLOT(scanFinished()));
        }
    }
}

bool QConnmanEngine::connmanAvailable() const
{
    QMutexLocker locker(&mutex);
    return connmanIsAvailable;
}

void QConnmanEngine::initialize()
{
    m_services = netman->getServices("");
    foreach (const NetworkService *serv, m_services) {
        addServiceConfiguration(serv->path());
    }

    // Get current list of access points.
    getConfigurations();
}

void QConnmanEngine::servicesListChanged(const QStringList &list)
{
    if (list == serviceNetworks)
        return;
    QStringList oldServices = serviceNetworks;
    Q_FOREACH (const QString &path, list) {
        if (!oldServices.contains(path)) {
            addServiceConfiguration(path);
        }
    }

    Q_FOREACH (const QString &path, oldServices) {
        if (!list.contains(path)) {
            removeConfiguration(QString::number(qHash(path)));
        }
    }
    m_services = netman->getServices("");
}

QList<QNetworkConfigurationPrivate *> QConnmanEngine::getConfigurations()
{
    QMutexLocker locker(&mutex);
    QList<QNetworkConfigurationPrivate *> fetchedConfigurations;
    QNetworkConfigurationPrivate* cpPriv = 0;

    for (int i = 0; i < foundConfigurations.count(); ++i) {
        QNetworkConfigurationPrivate *config = new QNetworkConfigurationPrivate;
        cpPriv = foundConfigurations.at(i);

        config->name = cpPriv->name;
        config->isValid = cpPriv->isValid;
        config->id = cpPriv->id;
        config->state = cpPriv->state;
        config->type = cpPriv->type;
        config->roamingSupported = cpPriv->roamingSupported;
        config->purpose = cpPriv->purpose;
        config->bearerType = cpPriv->bearerType;

        fetchedConfigurations.append(config);
        delete config;
    }
    return fetchedConfigurations;
}

void QConnmanEngine::doRequestUpdate()
{
    qDebug();
    netman->getTechnology("wifi")->scan();
}

void QConnmanEngine::scanFinished()
{
    qDebug();
    getConfigurations();
    emit updateCompleted();
}

QString QConnmanEngine::getInterfaceFromId(const QString &id)
{
    QMutexLocker locker(&mutex);
    return configInterfaces.value(id);
}

bool QConnmanEngine::hasIdentifier(const QString &id)
{
    QMutexLocker locker(&mutex);
    return accessPointConfigurations.contains(id);
}

void QConnmanEngine::connectToId(const QString &id)
{
    QMutexLocker locker(&mutex);
    QString servicePath = serviceFromId(id);
    NetworkService *serv = serviceLookup(servicePath);
    if (serv) {
        serv->requestConnect();
        return;
    }
    emit connectionError(id, QBearerEngineImpl::InterfaceLookupError);
}

void QConnmanEngine::disconnectFromId(const QString &id)
{
    QMutexLocker locker(&mutex);
    QString servicePath = serviceFromId(id);
    NetworkService *serv = serviceLookup(servicePath);
    if (serv) {
        serv->requestDisconnect();
        return;
    }
    emit connectionError(id, DisconnectionError);
}

NetworkService *QConnmanEngine::serviceLookup(const QString &servicePath)
{
    Q_FOREACH (NetworkService *serv, m_services) {
        if (serv->path() == servicePath) {
            return serv;
        }
    }
    return 0;
}

void QConnmanEngine::requestUpdate()
{
    qDebug();
    QMutexLocker locker(&mutex);
    QTimer::singleShot(0, this, SLOT(doRequestUpdate()));
}

QString QConnmanEngine::serviceFromId(const QString &id)
{
    QMutexLocker locker(&mutex);
    foreach (const QString &service, serviceNetworks) {
        if (id == QString::number(qHash(service)))
            return service;
    }

    return QString();
}

QNetworkSession::State QConnmanEngine::sessionStateForId(const QString &id)
{
    QMutexLocker locker(&mutex);

    QNetworkConfigurationPrivatePointer ptr = accessPointConfigurations.value(id);

    if (!ptr)
        return QNetworkSession::Invalid;

    if (!ptr->isValid) {
        return QNetworkSession::Invalid;

    }
    QString service = serviceFromId(id);
    NetworkService *serv = serviceLookup(service);
    QString servState = serv->state();

    if(serv->favorite() && (servState == "idle" || servState == "failure")) {
        return QNetworkSession::Disconnected;
    }

    if(servState == "association" || servState == "configuration" || servState == "login") {
        return QNetworkSession::Connecting;
    }
    if(servState == "ready" || servState == "online") {
        return QNetworkSession::Connected;
    }

    if ((ptr->state & QNetworkConfiguration::Discovered) ==
            QNetworkConfiguration::Discovered) {
        return QNetworkSession::Disconnected;
    } else if ((ptr->state & QNetworkConfiguration::Defined) == QNetworkConfiguration::Defined) {
        return QNetworkSession::NotAvailable;
    } else if ((ptr->state & QNetworkConfiguration::Undefined) ==
               QNetworkConfiguration::Undefined) {
        return QNetworkSession::NotAvailable;
    }

    return QNetworkSession::Invalid;
}

quint64 QConnmanEngine::bytesWritten(const QString &id)
{//TODO use connman counter API
    QMutexLocker locker(&mutex);
    quint64 result = 0;
    QString devFile = getInterfaceFromId(id);
    QFile tx("/sys/class/net/"+devFile+"/statistics/tx_bytes");
    if (tx.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&tx);
        in >> result;
        tx.close();
    }

    return result;
}

quint64 QConnmanEngine::bytesReceived(const QString &id)
{//TODO use connman counter API
    QMutexLocker locker(&mutex);
    quint64 result = 0;
    QString devFile = getInterfaceFromId(id);
    QFile rx("/sys/class/net/"+devFile+"/statistics/rx_bytes");
    if (rx.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&rx);
        in >> result;
        rx.close();
    }
    return result;
}

quint64 QConnmanEngine::startTime(const QString &/*id*/)
{
    // TODO
    QMutexLocker locker(&mutex);
    if (activeTime.isNull()) {
        return 0;
    }
    return activeTime.secsTo(QDateTime::currentDateTime());
}

QNetworkConfigurationManager::Capabilities QConnmanEngine::capabilities() const
{
    return QNetworkConfigurationManager::ForcedRoaming |
            QNetworkConfigurationManager::DataStatistics |
            QNetworkConfigurationManager::CanStartAndStopInterfaces |
            QNetworkConfigurationManager::NetworkSessionRequired;
}

QNetworkSessionPrivate *QConnmanEngine::createSessionBackend()
{
    return new QNetworkSessionPrivateImpl;
}

QNetworkConfigurationPrivatePointer QConnmanEngine::defaultConfiguration()
{
    return QNetworkConfigurationPrivatePointer();
}

void QConnmanEngine::serviceAdded(const QString &servicePath)
{
    addServiceConfiguration(servicePath);
}

void QConnmanEngine::serviceRemoved(const QString &servicePath)
{
    removeConfiguration(QString::number(qHash(servicePath)));
}

void QConnmanEngine::serviceStateChanged(const QString &state)
{
    NetworkService *serv = static_cast<NetworkService *>(sender());
    configurationChange(QString::number(qHash(serv->path())));
}

void QConnmanEngine::technologyConnectedChanged(bool connected)
{
}

void QConnmanEngine::configurationChange(const QString &id)
{
    QMutexLocker locker(&mutex);
    if (accessPointConfigurations.contains(id)) {
        bool changed = false;
        QNetworkConfigurationPrivatePointer ptr = accessPointConfigurations.value(id);

        QString servicePath = serviceFromId(id);
        NetworkService *serv = serviceLookup(servicePath);

        QString networkName = serv->name();

        QNetworkConfiguration::StateFlags curState = getStateForService(servicePath);

        ptr->mutex.lock();

        if (!ptr->isValid) {
            ptr->isValid = true;
        }

        if (ptr->name != networkName) {
            ptr->name = networkName;
            changed = true;
        }

        if (ptr->state != curState) {
            ptr->state = curState;
            changed = true;
        }

        ptr->mutex.unlock();

        if (changed) {
            locker.unlock();
            emit configurationChanged(ptr);
            locker.relock();
        }
    }

    locker.unlock();
    emit updateCompleted();
}

QNetworkConfiguration::StateFlags QConnmanEngine::getStateForService(const QString &servicePath)
{
    QMutexLocker locker(&mutex);
    NetworkService *serv = serviceLookup(servicePath);

    QNetworkConfiguration::StateFlags flag = QNetworkConfiguration::Defined;
    if (serv->type() == "cellular") {
        if (!serv->autoConnect() || (serv->roaming() && isAlwaysAskRoaming())) {
            flag = ( flag | QNetworkConfiguration::Defined);
        } else {
            flag = ( flag | QNetworkConfiguration::Discovered);
        }
    } else {
        if (serv->favorite()) {
            if (serv->autoConnect()) {
                flag = ( flag | QNetworkConfiguration::Discovered);
            }
        } else {
            flag = QNetworkConfiguration::Undefined;
        }
    }

    if (serv->state() == "ready" || serv->state() == "online") {
        flag = ( flag | QNetworkConfiguration::Active);
    }

    return flag;
}

QNetworkConfiguration::BearerType QConnmanEngine::typeToBearer(const QString &type)
{
    if (type == "wifi")
        return QNetworkConfiguration::BearerWLAN;
    if (type == "ethernet")
        return QNetworkConfiguration::BearerEthernet;
    if (type == "bluetooth")
        return QNetworkConfiguration::BearerBluetooth;
    if (type == "cellular") {
        return ofonoTechToBearerType(type);
    }
    if (type == "wimax")
        return QNetworkConfiguration::BearerWiMAX;

    //    if(type == "gps")
    //    if(type == "vpn")

    return QNetworkConfiguration::BearerUnknown;
}

QNetworkConfiguration::BearerType QConnmanEngine::ofonoTechToBearerType(const QString &/*type*/)
{
    if (ofonoManager.available()) {
        QOfonoNetworkRegistration ofonoNetwork(this);
        ofonoNetwork.setModemPath(currentModemPath());

        if(ofonoNetwork.isValid()) {
            foreach (const QString &op,ofonoNetwork.networkOperators() ) {
                QOfonoNetworkOperator opIface(this);
                opIface.setOperatorPath(op);
                foreach (const QString &opTech, opIface.technologies()) {

                    if(opTech == "gsm") {
                        return QNetworkConfiguration::Bearer2G;
                    }
                    if(opTech == "edge"){
                        return QNetworkConfiguration::BearerCDMA2000; //wrong, I know
                    }
                    if(opTech == "umts"){
                        return QNetworkConfiguration::BearerWCDMA;
                    }
                    if(opTech == "hspa"){
                        return QNetworkConfiguration::BearerHSPA;
                    }
                    if(opTech == "lte"){
                        return QNetworkConfiguration::BearerWiMAX; //not exact
                    }
                }
            }
        }
    }
    return QNetworkConfiguration::BearerUnknown;
}

bool QConnmanEngine::isRoamingAllowed(const QString &context)
{
    QOfonoConnectionManager dc(this);
    dc.setModemPath(currentModemPath());
    foreach (const QString &dcPath,dc.contexts()) {
        if(dcPath.contains(context.section("_",-1))) {
            return dc.roamingAllowed();
        }
    }
    return false;
}

void QConnmanEngine::removeConfiguration(const QString &id)
{
    QMutexLocker locker(&mutex);
    m_services = netman->getServices("");

    if (accessPointConfigurations.contains(id)) {

        QString servicePath = serviceFromId(id);
        serviceNetworks.removeOne(servicePath);

        QNetworkConfigurationPrivatePointer ptr = accessPointConfigurations.take(id);
        foundConfigurations.removeOne(ptr.data());
        locker.unlock();
        emit configurationRemoved(ptr);
        locker.relock();
    }
}

void QConnmanEngine::addServiceConfiguration(const QString &servicePath)
{
    QMutexLocker locker(&mutex);
    m_services = netman->getServices("");

    const QString id = QString::number(qHash(servicePath));

    if (!accessPointConfigurations.contains(id)) {

        NetworkService *serv = serviceLookup(servicePath);
        if (!serv) {
            return;
        }
        serviceNetworks.append(servicePath);
        connect(serv,SIGNAL(stateChanged(QString)),
                this,SLOT(serviceStateChanged(QString)));


        QNetworkConfigurationPrivate* cpPriv = new QNetworkConfigurationPrivate();

        QString networkName = serv->name();

        const QString connectionType = serv->type();
        if (connectionType == "ethernet") {
            cpPriv->bearerType = QNetworkConfiguration::BearerEthernet;
        } else if (connectionType == "wifi") {
            cpPriv->bearerType = QNetworkConfiguration::BearerWLAN;
        } else if (connectionType == "cellular") {
            cpPriv->bearerType = ofonoTechToBearerType("cellular");
            cpPriv->roamingSupported = isRoamingAllowed(servicePath);
        } else if (connectionType == "wimax") {
            cpPriv->bearerType = QNetworkConfiguration::BearerWiMAX;
        } else {
            cpPriv->bearerType = QNetworkConfiguration::BearerUnknown;
        }

        cpPriv->name = networkName;
        cpPriv->isValid = true;
        cpPriv->id = id;
        cpPriv->type = QNetworkConfiguration::InternetAccessPoint;

        if (serv->security().contains("none")) {
            cpPriv->purpose = QNetworkConfiguration::PublicPurpose;
        } else {
            cpPriv->purpose = QNetworkConfiguration::PrivatePurpose;
        }

        cpPriv->state = getStateForService(servicePath);

        QNetworkConfigurationPrivatePointer ptr(cpPriv);
        accessPointConfigurations.insert(ptr->id, ptr);
        foundConfigurations.append(cpPriv);
        configInterfaces[cpPriv->id] = serv->ethernet()["Interface"].toString();

        locker.unlock();
        emit configurationAdded(ptr);
        locker.relock();
        emit updateCompleted();
    }
}

bool QConnmanEngine::requiresPolling() const
{
    return false;
}

bool QConnmanEngine::isAlwaysAskRoaming()
{
    QSettings confFile(QStringLiteral("nemomobile"),QStringLiteral("connectionagent"));
    confFile.beginGroup(QStringLiteral("Connectionagent"));
    return confFile.value(QStringLiteral("askForRoaming")).toBool();
}

QString QConnmanEngine::currentModemPath()
{
    if (ofonoManager.available()) {
        foreach (const QString &modemPath, manager.modems()) {
            QOfonoModem device;
            device.setModemPath(modemPath);
            if (device.powered() && device.online())
                return modemPath;
        }
    }
    return QString();
}

QT_END_NAMESPACE

#endif // QT_NO_DBUS
#endif // QT_NO_BEARERMANAGEMENT
