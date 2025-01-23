#include "MinioApi.h"
#include <QMessageAuthenticationCode>
#include <QUrl>
#include <QBuffer>

MinioApi::MinioApi(const QString& endpoint, 
                   const QString& accessKey, 
                   const QString& secretKey,
                   const QString& region,
                   QObject* parent) 
    : QObject(parent)
    , m_endpoint(endpoint)
    , m_accessKey(accessKey)
    , m_secretKey(secretKey)
    , m_region(region)
{
    m_networkManager = new QNetworkAccessManager(this);
}

void MinioApi::uploadFile(const QString& bucketName, const QString& objectKey, const QString& filePath) {
    QFile* file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        emit uploadFinished(false, "Cannot open file");
        file->deleteLater();
        return;
    }

    QByteArray fileData = file->readAll();
    file->deleteLater();

    // Calculer le MD5 du fichier
    QByteArray contentMD5 = QCryptographicHash::hash(fileData, QCryptographicHash::Md5).toBase64();

    // Préparer la requête
    QString host = QUrl(m_endpoint).host();
    QString amzDate = QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmssZ");
    QString dateStamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd");

    // Construire l'URL
    QString resourcePath = QString("/%1/%2").arg(bucketName, objectKey);
    QUrl url(m_endpoint + resourcePath);

    // Créer la requête
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    request.setRawHeader("Host", host.toUtf8());
    request.setRawHeader("x-amz-date", amzDate.toUtf8());
    request.setRawHeader("x-amz-content-sha256", QCryptographicHash::hash(fileData, QCryptographicHash::Sha256).toHex());
    request.setRawHeader("Content-MD5", contentMD5);

    // Signature AWS v4
    QString canonicalRequest = createCanonicalRequest("PUT", resourcePath, "", 
        {
            {"host", host},
            {"x-amz-content-sha256", request.rawHeader("x-amz-content-sha256")},
            {"x-amz-date", amzDate}
        },
        QCryptographicHash::hash(fileData, QCryptographicHash::Sha256).toHex());

    QString stringToSign = createStringToSign(dateStamp, m_region, canonicalRequest);
    QByteArray signature = calculateSignature(dateStamp, m_region, stringToSign);

    QString auth = QString("AWS4-HMAC-SHA256 Credential=%1/%2/%3/s3/aws4_request, "
                         "SignedHeaders=host;x-amz-content-sha256;x-amz-date, Signature=%4")
                      .arg(m_accessKey)
                      .arg(dateStamp)
                      .arg(m_region)
                      .arg(QString(signature.toHex()));

    request.setRawHeader("Authorization", auth.toUtf8());

    // Envoyer la requête
    QNetworkReply* reply = m_networkManager->put(request, fileData);

    connect(reply, &QNetworkReply::uploadProgress, this, &MinioApi::uploadProgress);
    connect(reply, &QNetworkReply::finished, [=]() {
        if (reply->error() == QNetworkReply::NoError) {
            emit uploadFinished(true, "Upload successful");
        } else {
            emit uploadFinished(false, reply->errorString());
        }
        reply->deleteLater();
    });
}

QString MinioApi::createCanonicalRequest(const QString& method, 
                                       const QString& uri,
                                       const QString& queryString,
                                       const QMap<QString, QString>& headers,
                                       const QString& payloadHash) {
    QStringList canonicalHeaders;
    QStringList signedHeaders;
    
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        canonicalHeaders << QString("%1:%2\n").arg(it.key().toLower(), it.value());
        signedHeaders << it.key().toLower();
    }

    return QString("%1\n%2\n%3\n%4\n%5\n%6")
        .arg(method)
        .arg(uri)
        .arg(queryString)
        .arg(canonicalHeaders.join(""))
        .arg(signedHeaders.join(";"))
        .arg(payloadHash);
}

QString MinioApi::createStringToSign(const QString& dateStamp, 
                                   const QString& region,
                                   const QString& canonicalRequest) {
    QString hashedRequest = QCryptographicHash::hash(
        canonicalRequest.toUtf8(), 
        QCryptographicHash::Sha256).toHex();

    return QString("AWS4-HMAC-SHA256\n%1\n%2/%3/s3/aws4_request\n%4")
        .arg(dateStamp)
        .arg(dateStamp)
        .arg(region)
        .arg(hashedRequest);
}

QByteArray MinioApi::calculateSignature(const QString& dateStamp,
                                      const QString& region,
                                      const QString& stringToSign) {
    QByteArray kDate = QMessageAuthenticationCode::hash(
        dateStamp.toUtf8(),
        QByteArray("AWS4" + m_secretKey.toUtf8()),
        QCryptographicHash::Sha256);

    QByteArray kRegion = QMessageAuthenticationCode::hash(
        region.toUtf8(), 
        kDate,
        QCryptographicHash::Sha256);

    QByteArray kService = QMessageAuthenticationCode::hash(
        QByteArray("s3"),
        kRegion,
        QCryptographicHash::Sha256);

    QByteArray kSigning = QMessageAuthenticationCode::hash(
        QByteArray("aws4_request"),
        kService,
        QCryptographicHash::Sha256);

    return QMessageAuthenticationCode::hash(
        stringToSign.toUtf8(),
        kSigning,
        QCryptographicHash::Sha256);
}