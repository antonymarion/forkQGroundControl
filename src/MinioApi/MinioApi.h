#ifndef MINIO_API_H
#define MINIO_API_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QDateTime>
#include <QCryptographicHash>
#include <QFile>
#include <QMap>

class MinioApi : public QObject {
    Q_OBJECT

public:
    explicit MinioApi(const QString& endpoint, 
                     const QString& accessKey, 
                     const QString& secretKey,
                     const QString& region = "eu-west-3",
                     QObject* parent = nullptr);

    void uploadFile(const QString& bucketName, 
                   const QString& objectKey, 
                   const QString& filePath);

signals:
    void uploadProgress(qint64 bytesSent, qint64 bytesTotal);
    void uploadFinished(bool success, const QString& message);

private:
    QString createCanonicalRequest(const QString& method, 
                                 const QString& uri,
                                 const QString& queryString,
                                 const QMap<QString, QString>& headers,
                                 const QString& payloadHash);

    QString createStringToSign(const QString& dateStamp, 
                             const QString& region,
                             const QString& canonicalRequest);

    QByteArray calculateSignature(const QString& dateStamp,
                                const QString& region,
                                const QString& stringToSign);

private:
    QNetworkAccessManager* m_networkManager;
    QString m_endpoint;
    QString m_accessKey;
    QString m_secretKey;
    QString m_region;
};

#endif // MINIO_API_H