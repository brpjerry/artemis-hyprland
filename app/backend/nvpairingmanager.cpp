#include "nvpairingmanager.h"
#include "utils.h"

#include <exception>
#include <stdexcept>

#include <QScopeGuard>
#include <QUrl>
#include <QXmlStreamReader>

#include <openssl/bio.h>
#include <openssl/rand.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/evp.h>

#define REQUEST_TIMEOUT_MS 5000

NvPairingManager::NvPairingManager(NvComputer* computer) :
    m_Http(computer)
{
    QByteArray cert = IdentityManager::get()->getCertificate();
    BIO *bio = BIO_new_mem_buf(cert.data(), -1);
    THROW_BAD_ALLOC_IF_NULL(bio);

    m_Cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free_all(bio);
    if (m_Cert == nullptr)
    {
        throw std::runtime_error("Unable to load certificate");
    }

    QByteArray pk = IdentityManager::get()->getPrivateKey();
    bio = BIO_new_mem_buf(pk.data(), -1);
    THROW_BAD_ALLOC_IF_NULL(bio);

    m_PrivateKey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free_all(bio);
    if (m_PrivateKey == nullptr)
    {
        throw std::runtime_error("Unable to load private key");
    }
}

NvPairingManager::~NvPairingManager()
{
    X509_free(m_Cert);
    EVP_PKEY_free(m_PrivateKey);
}

QByteArray
NvPairingManager::generateRandomBytes(int length)
{
    QByteArray data(length, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(data.data()), length) != 1) {
        qCritical() << "Unable to generate random bytes for pairing";
        return QByteArray();
    }
    return data;
}

QByteArray
NvPairingManager::encrypt(const QByteArray& plaintext, const QByteArray& key)
{
    if (plaintext.isEmpty()) {
        return QByteArray();
    }

    EVP_CIPHER_CTX* cipher = EVP_CIPHER_CTX_new();
    THROW_BAD_ALLOC_IF_NULL(cipher);

    Q_ASSERT(key.length() == 16);
    EVP_EncryptInit(cipher, EVP_aes_128_ecb(), reinterpret_cast<const unsigned char*>(key.data()), NULL);
    EVP_CIPHER_CTX_set_padding(cipher, 0);

    QByteArray ciphertext(plaintext.size(), 0);
    int ciphertextLen;
    EVP_EncryptUpdate(cipher,
                      reinterpret_cast<unsigned char*>(ciphertext.data()),
                      &ciphertextLen,
                      reinterpret_cast<const unsigned char*>(plaintext.data()),
                      plaintext.length());
    Q_ASSERT(ciphertextLen == ciphertext.length());

    EVP_CIPHER_CTX_free(cipher);

    return ciphertext;
}

QByteArray
NvPairingManager::decrypt(const QByteArray& ciphertext, const QByteArray& key)
{
    if (ciphertext.isEmpty()) {
        return QByteArray();
    }

    EVP_CIPHER_CTX* cipher = EVP_CIPHER_CTX_new();
    THROW_BAD_ALLOC_IF_NULL(cipher);

    Q_ASSERT(key.length() == 16);
    EVP_DecryptInit(cipher, EVP_aes_128_ecb(), reinterpret_cast<const unsigned char*>(key.data()), NULL);
    EVP_CIPHER_CTX_set_padding(cipher, 0);

    QByteArray plaintext(ciphertext.size(), 0);
    int plaintextLen = 0;
    if (EVP_DecryptUpdate(cipher,
                          reinterpret_cast<unsigned char*>(plaintext.data()),
                          &plaintextLen,
                          reinterpret_cast<const unsigned char*>(ciphertext.data()),
                          ciphertext.length()) != 1 ||
            plaintextLen != plaintext.length()) {
        // A failure, or a partial block from a malformed reply. The callers'
        // size checks reject the empty result.
        plaintext.clear();
    }

    EVP_CIPHER_CTX_free(cipher);

    return plaintext;
}

QByteArray
NvPairingManager::getSignatureFromCert(X509* cert)
{
#if (OPENSSL_VERSION_NUMBER < 0x10002000L)
    ASN1_BIT_STRING *asnSignature = cert->signature;
#elif (OPENSSL_VERSION_NUMBER < 0x10100000L)
    ASN1_BIT_STRING *asnSignature;
    X509_get0_signature(&asnSignature, NULL, cert);
#else
    const ASN1_BIT_STRING *asnSignature;
    X509_get0_signature(&asnSignature, NULL, cert);
#endif

    return QByteArray(
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
        reinterpret_cast<const char*>(ASN1_STRING_data(asnSignature)),
#else
        reinterpret_cast<const char*>(ASN1_STRING_get0_data(asnSignature)),
#endif
#if (OPENSSL_VERSION_NUMBER < 0x40100000L)
        ASN1_STRING_length(asnSignature)
#else
        ASN1_STRING_get_length(asnSignature)
#endif
    );
}

QByteArray
NvPairingManager::getSignatureFromPemCert(const QByteArray& certificate)
{
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    BIO* bio = BIO_new_mem_buf(const_cast<char*>(certificate.data()), -1);
#else
    BIO* bio = BIO_new_mem_buf(certificate.data(), -1);
#endif
    THROW_BAD_ALLOC_IF_NULL(bio);

    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free_all(bio);
    if (cert == nullptr) {
        return QByteArray();
    }

    QByteArray signature = getSignatureFromCert(cert);
    X509_free(cert);

    return signature;
}

bool
NvPairingManager::verifySignature(const QByteArray& data, const QByteArray& signature, const QByteArray& serverCertificate)
{
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    BIO* bio = BIO_new_mem_buf(const_cast<char*>(serverCertificate.data()), -1);
#else
    BIO* bio = BIO_new_mem_buf(serverCertificate.data(), -1);
#endif
    THROW_BAD_ALLOC_IF_NULL(bio);

    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free_all(bio);
    if (cert == nullptr) {
        return false;
    }

    EVP_PKEY* pubKey = X509_get_pubkey(cert);
    if (pubKey == nullptr) {
        X509_free(cert);
        return false;
    }

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    EVP_MD_CTX* mdctx = EVP_MD_CTX_create();
#else
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
#endif
    THROW_BAD_ALLOC_IF_NULL(mdctx);

    // Stop at the first failure: a key that can't do SHA-256 verification
    // fails initialization, and the later calls are undefined after that
    const bool verified =
            EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pubKey) == 1 &&
            EVP_DigestVerifyUpdate(mdctx, data.data(), data.length()) == 1 &&
            EVP_DigestVerifyFinal(mdctx, reinterpret_cast<unsigned char*>(const_cast<char*>(signature.data())), signature.length()) == 1;

    EVP_PKEY_free(pubKey);
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    EVP_MD_CTX_destroy(mdctx);
#else
    EVP_MD_CTX_free(mdctx);
#endif
    X509_free(cert);

    return verified;
}

QByteArray
NvPairingManager::signMessage(const QByteArray& message)
{
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    EVP_MD_CTX* ctx = EVP_MD_CTX_create();
#else
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
#endif
    THROW_BAD_ALLOC_IF_NULL(ctx);

    EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, m_PrivateKey);
    EVP_DigestSignUpdate(ctx, reinterpret_cast<unsigned char*>(const_cast<char*>(message.data())), message.length());

    size_t signatureLength = 0;
    EVP_DigestSignFinal(ctx, NULL, &signatureLength);

    QByteArray signature((int)signatureLength, 0);
    EVP_DigestSignFinal(ctx, reinterpret_cast<unsigned char*>(signature.data()), &signatureLength);

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    EVP_MD_CTX_destroy(ctx);
#else
    EVP_MD_CTX_free(ctx);
#endif

    return signature;
}

QByteArray
NvPairingManager::saltPin(const QByteArray& salt, QString pin)
{
    return QByteArray().append(salt).append(pin.toUtf8());
}

// Apollo reports an unusable OTP in the root's status_message, with status 200
static QString
getRootStatusMessage(const QString& xml)
{
    QXmlStreamReader xmlReader(xml);
    while (xmlReader.readNextStartElement()) {
        if (xmlReader.name() == QLatin1String("root")) {
            return xmlReader.attributes().value("status_message").toString();
        }
    }
    return QString();
}

// Ends a failed attempt on the host. GFE drops it on unpair. Apollo has no
// unpair endpoint (the 404 would throw), and it keeps the attempt's session
// until a pairing request arrives out of order: the next getservercert would
// fail and use up its OTP. An empty clientpairingsecret is out of order or too
// short in every phase, so Apollo drops the session.
void
NvPairingManager::cancelPairing(bool apolloOtp)
{
    if (!apolloOtp) {
        m_Http.openConnectionToString(m_Http.m_BaseUrlHttp, "unpair", nullptr, REQUEST_TIMEOUT_MS);
        return;
    }

    try {
        m_Http.openConnectionToString(m_Http.m_BaseUrlHttp,
                                      "pair",
                                      "devicename=roth&updateState=1&clientpairingsecret=",
                                      REQUEST_TIMEOUT_MS);
    } catch (...) {
        // Best effort: the attempt has already failed, and this also runs
        // while its exception unwinds
    }
}

NvPairingManager::PairState
NvPairingManager::pair(QString appVersion, QString pin, QSslCertificate& serverCert, const OtpAuth* otp)
{
    const bool apolloOtp = (otp != nullptr);

    int serverMajorVersion = NvHTTP::parseQuad(appVersion).value(0);
    qInfo() << "Pairing with server generation:" << serverMajorVersion;

    QCryptographicHash::Algorithm hashAlgo;
    int hashLength;
    if (apolloOtp || serverMajorVersion >= 7)
    {
        // Gen 7+ and Apollo OTP use SHA-256 hashing. (The OTP host's
        // appversion is unauthenticated, so it can't pick SHA-1.)
        hashAlgo = QCryptographicHash::Sha256;
        hashLength = 32;
    }
    else
    {
        // Prior to Gen 7 uses SHA-1 hashing
        hashAlgo = QCryptographicHash::Sha1;
        hashLength = 20;
    }

    QByteArray salt = generateRandomBytes(16);
    if (salt.isEmpty()) {
        return PairState::FAILED;
    }
    QByteArray saltedPin = saltPin(salt, pin);

    // For OTP, a failure that throws (a network error or an error status)
    // must also end the host's session, or the next attempt loses its OTP
    const int uncaughtAtStart = std::uncaught_exceptions();
    const auto cancelOnException = qScopeGuard([&] {
        if (apolloOtp && std::uncaught_exceptions() > uncaughtAtStart) {
            cancelPairing(apolloOtp);
        }
    });

    // verifyResponseStatus() logs the status message and puts it in the error
    // shown to the user. An OTP reply's message could echo the request, so
    // remove otpauth and the salt from it first.
    QStringList otpSecrets;
    auto verifyStatus = [&otpSecrets](const QString& xml) {
        QString checked = xml;
        for (const QString& secret : std::as_const(otpSecrets)) {
            checked.replace(secret, QStringLiteral("REDACTED"), Qt::CaseInsensitive);
        }
        NvHTTP::verifyResponseStatus(checked);
    };

    QByteArray aesKey = QCryptographicHash::hash(saltedPin, hashAlgo);
    aesKey.truncate(16);

    QString getCert;
    if (apolloOtp) {
        // Apollo answers at once, with the OTP's key if otpauth matches and a
        // random key that fails the PIN check below if it doesn't. Don't log
        // the URL: with the salt, otpauth allows an offline search for the
        // OTP and passphrase.
        QString otpAuth = QCryptographicHash::hash((pin + salt.toHex() + otp->passphrase).toUtf8(),
                                                   QCryptographicHash::Sha256).toHex().toUpper();
        otpSecrets << otpAuth << QString::fromLatin1(salt.toHex());
        getCert = m_Http.openConnectionToString(m_Http.m_BaseUrlHttp,
                                                "pair",
                                                "devicename=" + QString::fromLatin1(QUrl::toPercentEncoding(otp->deviceName)) +
                                                "&updateState=1&phrase=getservercert&salt=" + salt.toHex() +
                                                "&clientcert=" + IdentityManager::get()->getCertificate().toHex() +
                                                "&otpauth=" + otpAuth,
                                                REQUEST_TIMEOUT_MS,
                                                NvHTTP::NvLogLevel::NVLL_NONE);
    }
    else {
        getCert = m_Http.openConnectionToString(m_Http.m_BaseUrlHttp,
                                                "pair",
                                                "devicename=roth&updateState=1&phrase=getservercert&salt=" +
                                                salt.toHex() + "&clientcert=" + IdentityManager::get()->getCertificate().toHex(),
                                                0);
    }
    verifyStatus(getCert);
    if (apolloOtp && getRootStatusMessage(getCert) == "OTP auth not available.") {
        qCritical() << "No OTP available on the host";
        cancelPairing(apolloOtp);
        return PairState::OTP_UNAVAILABLE;
    }
    if (NvHTTP::getXmlString(getCert, "paired") != "1")
    {
        qCritical() << "Failed pairing at stage #1";
        if (apolloOtp) {
            cancelPairing(apolloOtp);
        }
        return PairState::FAILED;
    }

    QByteArray serverCertStr = NvHTTP::getXmlStringFromHex(getCert, "plaincert");
    if (serverCertStr.isEmpty()) {
        qCritical() << "Server likely already pairing";
        cancelPairing(apolloOtp);
        return PairState::ALREADY_IN_PROGRESS;
    }

    QSslCertificate unverifiedServerCert = QSslCertificate(serverCertStr);
    if (unverifiedServerCert.isNull()) {
        qCritical() << "Failed to parse plaincert";
        cancelPairing(apolloOtp);
        return PairState::FAILED;
    }

    // Pin this cert for TLS until pairing is complete. If successful, we will propagate
    // the cert into the NvComputer object and persist it.
    m_Http.setServerCert(unverifiedServerCert);

    // Check the proof against the single certificate Qt took from the reply,
    // the one that will be pinned and saved. OpenSSL could pick a different
    // one from a reply with several PEM blocks.
    serverCertStr = unverifiedServerCert.toPem();

    QByteArray randomChallenge = generateRandomBytes(16);
    if (randomChallenge.isEmpty()) {
        cancelPairing(apolloOtp);
        return PairState::FAILED;
    }
    QByteArray encryptedChallenge = encrypt(randomChallenge, aesKey);
    QString challengeXml = m_Http.openConnectionToString(m_Http.m_BaseUrlHttp,
                                                         "pair",
                                                         "devicename=roth&updateState=1&clientchallenge=" +
                                                         encryptedChallenge.toHex(),
                                                         REQUEST_TIMEOUT_MS);
    verifyStatus(challengeXml);
    if (NvHTTP::getXmlString(challengeXml, "paired") != "1")
    {
        qCritical() << "Failed pairing at stage #2";
        cancelPairing(apolloOtp);
        return PairState::FAILED;
    }

    QByteArray challengeResponseData = decrypt(m_Http.getXmlStringFromHex(challengeXml, "challengeresponse"), aesKey);
    if (challengeResponseData.size() < hashLength + 16) {
        qCritical() << "Invalid challengeresponse at stage #2";
        cancelPairing(apolloOtp);
        return PairState::FAILED;
    }

    QByteArray clientSecretData = generateRandomBytes(16);
    if (clientSecretData.isEmpty()) {
        cancelPairing(apolloOtp);
        return PairState::FAILED;
    }
    QByteArray challengeResponse;
    QByteArray serverResponse(challengeResponseData.data(), hashLength);

    challengeResponse.append(challengeResponseData.data() + hashLength, 16);
    challengeResponse.append(getSignatureFromCert(m_Cert));
    challengeResponse.append(clientSecretData);

    QByteArray paddedHash = QCryptographicHash::hash(challengeResponse, hashAlgo);
    paddedHash.resize(32);
    QByteArray encryptedChallengeResponseHash = encrypt(paddedHash, aesKey);
    QString respXml = m_Http.openConnectionToString(m_Http.m_BaseUrlHttp,
                                                    "pair",
                                                    "devicename=roth&updateState=1&serverchallengeresp=" +
                                                    encryptedChallengeResponseHash.toHex(),
                                                    REQUEST_TIMEOUT_MS);
    verifyStatus(respXml);
    if (NvHTTP::getXmlString(respXml, "paired") != "1")
    {
        qCritical() << "Failed pairing at stage #3";
        cancelPairing(apolloOtp);
        return PairState::FAILED;
    }

    QByteArray pairingSecret = NvHTTP::getXmlStringFromHex(respXml, "pairingsecret");
    if (pairingSecret.size() <= 16) {
        qCritical() << "Invalid pairingsecret at stage #3";
        cancelPairing(apolloOtp);
        return PairState::FAILED;
    }

    QByteArray serverSecret = pairingSecret.left(16);
    QByteArray serverSignature = pairingSecret.mid(16);

    if (!verifySignature(serverSecret,
                         serverSignature,
                         serverCertStr))
    {
        qCritical() << "MITM detected";
        cancelPairing(apolloOtp);
        return PairState::FAILED;
    }

    QByteArray expectedResponseData;
    expectedResponseData.append(randomChallenge);
    expectedResponseData.append(getSignatureFromPemCert(serverCertStr));
    expectedResponseData.append(serverSecret);
    if (QCryptographicHash::hash(expectedResponseData, hashAlgo) != serverResponse)
    {
        qCritical() << "Incorrect PIN";
        cancelPairing(apolloOtp);
        return PairState::PIN_WRONG;
    }

    QByteArray clientPairingSecret;
    clientPairingSecret.append(clientSecretData);
    clientPairingSecret.append(signMessage(clientSecretData));

    QString secretRespXml = m_Http.openConnectionToString(m_Http.m_BaseUrlHttp,
                                                          "pair",
                                                          "devicename=roth&updateState=1&clientpairingsecret=" +
                                                          clientPairingSecret.toHex(),
                                                          REQUEST_TIMEOUT_MS);
    verifyStatus(secretRespXml);
    if (NvHTTP::getXmlString(secretRespXml, "paired") != "1")
    {
        qCritical() << "Failed pairing at stage #4";
        cancelPairing(apolloOtp);
        return PairState::FAILED;
    }

    QString pairChallengeXml = m_Http.openConnectionToString(m_Http.m_BaseUrlHttps,
                                                             "pair",
                                                             "devicename=roth&updateState=1&phrase=pairchallenge",
                                                             REQUEST_TIMEOUT_MS);
    verifyStatus(pairChallengeXml);
    if (NvHTTP::getXmlString(pairChallengeXml, "paired") != "1")
    {
        qCritical() << "Failed pairing at stage #5";
        cancelPairing(apolloOtp);
        return PairState::FAILED;
    }

    serverCert = std::move(unverifiedServerCert);
    return PairState::PAIRED;
}
