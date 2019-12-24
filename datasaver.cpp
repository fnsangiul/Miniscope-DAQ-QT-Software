#include "datasaver.h"

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QCoreApplication>

DataSaver::DataSaver(QObject *parent) :
    QObject(parent),
    m_recording(false),
    m_running(false)
{

}

void DataSaver::setupFilePaths()
{
    QString tempString, tempString2;
    QJsonArray directoryStructure = m_userConfig["directoryStructure"].toArray();

    // Construct and make base directory
    baseDirectory = m_userConfig["dataDirectory"].toString();
    for (int i = 0; i < directoryStructure.size(); i++) {
        tempString = directoryStructure[i].toString();
        if (tempString == "date")
            baseDirectory += "/" + recordStartDateTime.date().toString("yyyy_MM_dd");
        else if (tempString == "time")
            baseDirectory += "/" + recordStartDateTime.time().toString("HH_mm_ss");
        else if (tempString == "researcherName")
            baseDirectory += "/" + m_userConfig["researcherName"].toString().replace(" ", "_");
        else if (tempString == "experimentName")
            baseDirectory += "/" + m_userConfig["experimentName"].toString().replace(" ", "_");
        else if (tempString == "animalName")
            baseDirectory += "/" + m_userConfig["animalName"].toString().replace(" ", "_");
    }

    if (!QDir(baseDirectory).exists()) {
        if(!QDir().mkpath(baseDirectory))
            qDebug() << "Could not make path: " << baseDirectory;
    }
    else
        qDebug() << baseDirectory << " already exisits. This likely will cause issues";

    // TODO: save metadata in base directory for experiment. Maybe some thing like saveBaseMetaDataJscon();

    // Setup directories for each recording device
    QJsonObject devices = m_userConfig["devices"].toObject();

    for (int i = 0; i < devices["miniscopes"].toArray().size(); i++) { // Miniscopes
        tempString = devices["miniscopes"].toArray()[i].toObject()["deviceName"].toString();
        tempString2 = tempString;
        tempString2.replace(" ", "_");
        deviceDirectory[tempString] = baseDirectory + "/" + tempString2;
        QDir().mkdir(deviceDirectory[tempString]);
    }
    for (int i = 0; i < devices["cameras"].toArray().size(); i++) { // Cameras
        tempString = devices["miniscopes"].toArray()[i].toObject()["deviceName"].toString();
        tempString2 = tempString;
        tempString2.replace(" ", "_");
        deviceDirectory[tempString] = baseDirectory + "/" + tempString2;
        QDir().mkdir(deviceDirectory[tempString]);
    }
    // Experiment Directory
    QDir().mkdir(baseDirectory + "/experiment");
}

void DataSaver::setFrameBufferParameters(QString name,
                                         cv::Mat *frameBuf,
                                         qint64 *tsBuffer,
                                         QSemaphore *freeFrames,
                                         QSemaphore *usedFrames)
{
    frameBuffer[name] = frameBuf;
    timeStampBuffer[name] = tsBuffer;
    freeCount[name] = freeFrames;
    usedCount[name] = usedFrames;

    frameCount[name] = 0;
}

void DataSaver::startRunning()
{
    m_running = true;
    int i;
    QStringList names;
    while(m_running) {
        // for video streams
        names = frameBuffer.keys();
        for (i = 0; i < frameBuffer.size(); i++) {
            while (usedCount[names[i]]->tryAcquire()) {
                // grad info from buffer in a threadsafe way
                if (m_recording) {
                    // save frame to file
                }

                frameCount[names[i]]++;
                freeCount[names[i]]->release(1);
            }
        }
        QCoreApplication::processEvents(); // Is there a better way to do this. This is against best practices
    }
}

void DataSaver::startRecording()
{
    QJsonDocument jDoc;

    recordStartDateTime = QDateTime::currentDateTime();
    setupFilePaths();
    // TODO: Save meta data JSONs
    jDoc = constructBaseDirectoryMetaData();
    saveJson(jDoc, baseDirectory + "/metaData.json");

    QString deviceName;
    for (int i = 0; i < m_userConfig["devices"].toObject()["miniscopes"].toArray().size(); i++) {
        deviceName = m_userConfig["devices"].toObject()["miniscopes"].toArray()[i].toObject()["deviceName"].toString();
        jDoc = constructMiniscopeMetaData(i);
        saveJson(jDoc, deviceDirectory[deviceName] + "/metaData.json");
    }

    // TODO: Create data files
    m_recording = true;
}

void DataSaver::devicePropertyChanged(QString deviceName, QString propName, double propValue)
{
    deviceProperties[deviceName][propName] = propValue;
    qDebug() << deviceName << propName << propValue;
    // TODO: signal change to filing keeping track of changes during recording
}

QJsonDocument DataSaver::constructBaseDirectoryMetaData()
{
    QJsonObject metaData;
    QJsonDocument jDoc;

    metaData["researcherName"] = m_userConfig["researcherName"].toString();
    metaData["animalName"] = m_userConfig["animalName"].toString();
    metaData["experimentName"] = m_userConfig["experimentName"].toString();
    metaData["baseDirectory"] = baseDirectory;

    // Start time
    metaData["year"] = recordStartDateTime.date().year();
    metaData["month"] = recordStartDateTime.date().month();
    metaData["day"] = recordStartDateTime.date().day();
    metaData["hour"] = recordStartDateTime.time().hour();
    metaData["minute"] = recordStartDateTime.time().minute();
    metaData["second"] = recordStartDateTime.time().second();
    metaData["msec"] = recordStartDateTime.time().msec();
    metaData["msecSinceEpoch"] = recordStartDateTime.toMSecsSinceEpoch();

    //Device info
    QStringList list;
    for (int i = 0; i < m_userConfig["devices"].toObject()["miniscopes"].toArray().size(); i++)
        list.append(m_userConfig["devices"].toObject()["miniscopes"].toArray()[i].toObject()["deviceName"].toString());
    metaData["miniscopes"] = QJsonArray().fromStringList(list);

    list.clear();
    for (int i = 0; i < m_userConfig["devices"].toObject()["cameras"].toArray().size(); i++)
        list.append(m_userConfig["devices"].toObject()["cameras"].toArray()[i].toObject()["deviceName"].toString());
    metaData["cameras"] = QJsonArray().fromStringList(list);

    jDoc.setObject(metaData);
    return jDoc;
}

QJsonDocument DataSaver::constructMiniscopeMetaData(int idx)
{
    QJsonObject metaData;
    QJsonDocument jDoc;

    QJsonObject miniscope = m_userConfig["devices"].toObject()["miniscopes"].toArray()[idx].toObject();
    QString deviceName = miniscope["deviceName"].toString();

    metaData["deviceName"] = deviceName;
    metaData["deviceType"] = miniscope["deviceType"].toString();
    metaData["deviceDirectory"] = deviceDirectory[deviceName];

    // loop through device properties at the start of recording
    QStringList keys = deviceProperties[deviceName].keys();
    for (int i = 0; i < keys.length(); i++) {
        metaData[keys[i]] = deviceProperties[deviceName][keys[i]];
    }

    jDoc.setObject(metaData);
    return jDoc;
}

void DataSaver::saveJson(QJsonDocument document, QString fileName)
{
    QFile jsonFile(fileName);
    jsonFile.open(QFile::NewOnly);
    jsonFile.write(document.toJson());

}

