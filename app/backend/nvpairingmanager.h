#pragma once

#include "identitymanager.h"
#include "nvhttp.h"

#include <openssl/x509.h>
#include <openssl/evp.h>

class NvPairingManager
{
public:
    enum PairState
    {
        PAIRED,
        PIN_WRONG,
        FAILED,
        ALREADY_IN_PROGRESS,
        // Apollo OTP pairing: the host has no OTP, or it expired
        OTP_UNAVAILABLE
    };

    // Apollo's one-time PIN pairing. The host checks otpauth, a hash of the
    // OTP, the salt and the passphrase, on the first request instead of
    // waiting for a PIN to be entered. The rest is the PIN handshake with the
    // OTP as the PIN, so the host's proof is verified the same way.
    struct OtpAuth
    {
        QString passphrase;
        QString deviceName;
    };

    explicit NvPairingManager(NvComputer* computer);

    ~NvPairingManager();

    // Sets serverCert only when pairing succeeds
    PairState
    pair(QString appVersion, QString pin, QSslCertificate& serverCert, const OtpAuth* otp = nullptr);

private:
    void
    cancelPairing(bool apolloOtp);

    QByteArray
    generateRandomBytes(int length);

    QByteArray
    saltPin(const QByteArray& salt, QString pin);

    QByteArray
    encrypt(const QByteArray& plaintext, const QByteArray& key);

    QByteArray
    decrypt(const QByteArray& ciphertext, const QByteArray& key);

    QByteArray
    getSignatureFromCert(X509* cert);

    QByteArray
    getSignatureFromPemCert(const QByteArray& certificate);

    bool
    verifySignature(const QByteArray& data, const QByteArray& signature, const QByteArray& serverCertificate);

    QByteArray
    signMessage(const QByteArray& message);

    NvHTTP m_Http;
    X509* m_Cert;
    EVP_PKEY* m_PrivateKey;
};
