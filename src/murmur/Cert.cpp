/* Copyright (C) 2005-2007, Thorvald Natvig <thorvald@natvig.com>

   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   - Neither the name of the Mumble Developers nor the names of its
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "Cert.h"
#include "Server.h"

#include <openssl/pem.h>
#include <openssl/conf.h>
#include <openssl/x509v3.h>
#ifndef OPENSSL_NO_ENGINE
#include <openssl/engine.h>
#endif

int add_ext(X509 * crt, int nid, char *value)
{
    X509_EXTENSION *ex;
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, crt, crt, NULL, NULL, 0);
    ex = X509V3_EXT_conf_nid(NULL, &ctx, nid, value);
    if (!ex)
	return 0;

    X509_add_ext(crt, ex, -1);
    X509_EXTENSION_free(ex);
    return 1;
}

Cert cert;

Cert::Cert() : QObject() {
}

void Cert::initialize() {
  QByteArray crt, key, store;



  if (! g_sp.qsSSLCert.isEmpty()) {
    QFile pem(g_sp.qsSSLCert);
    if (pem.open(QIODevice::ReadOnly)) {
      crt = pem.readAll();
      pem.close();
    } else {
      qWarning("Failed to read %s", qPrintable(g_sp.qsSSLCert));
    }
  }
  if (! g_sp.qsSSLKey.isEmpty()) {
    QFile pem(g_sp.qsSSLKey);
    if (pem.open(QIODevice::ReadOnly)) {
      key = pem.readAll();
      pem.close();
    } else {
      qWarning("Failed to read %s", qPrintable(g_sp.qsSSLKey));
    }
  }
  QFile pem(g_sp.qsSSLStore);
  if (pem.open(QIODevice::ReadOnly)) {
    store = pem.readAll();
    pem.close();
  }
  if (! crt.isEmpty()) {
    qscCert = QSslCertificate(crt);
    if (qscCert.isNull()) {
      qWarning("Failed to parse certificate.");
    } 
  }

    if (! key.isEmpty() && qscCert.isNull()) {
      qscCert = QSslCertificate(key);	
      if (! qscCert.isNull()) {	
        qDebug("Using certificate from key file.");
      } 	
    }


  if (! qscCert.isNull()) {
    QSsl::KeyAlgorithm alg = qscCert.publicKey().algorithm();
    
    if (! key.isEmpty()) {
      qskKey = QSslKey(key, alg);
      if (qskKey.isNull()) {
        qWarning("Failed to parse key file.");
      }
    }

  if (! crt.isEmpty() && qskKey.isNull()) {
    qskKey = QSslKey(crt, alg);
    if (! qskKey.isNull()) {
      qDebug("Using key from certificate file.");
    } 
  }

  }
  
  if (qscCert.isNull() || qskKey.isNull()) {
    if (! key.isEmpty() || ! crt.isEmpty()) {
      qFatal("Certificate specified, but failed to load.");
    }
    qskKey = QSslKey(store, QSsl::Rsa);
    qscCert = QSslCertificate(store);
    if (qscCert.isNull() || qskKey.isNull()) {
      qWarning("Generating new server certificate.");
      
      BIO *bio_err;
      
      CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ON);
      
      bio_err=BIO_new_fp(stderr, BIO_NOCLOSE);

      X509 *x509 = X509_new();
      EVP_PKEY *pkey = EVP_PKEY_new();
      RSA *rsa = RSA_generate_key(1024,RSA_F4,NULL,NULL);
      EVP_PKEY_assign_RSA(pkey, rsa);
      
      X509_set_version(x509, 2);
      ASN1_INTEGER_set(X509_get_serialNumber(x509),1);
      X509_gmtime_adj(X509_get_notBefore(x509),0);
      X509_gmtime_adj(X509_get_notAfter(x509),60*60*24*365);
      X509_set_pubkey(x509, pkey);
      
      X509_NAME *name=X509_get_subject_name(x509);
      
      X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("Murmur Autogenerated Certificate"), -1, -1, 0);
      X509_set_issuer_name(x509, name);
      add_ext(x509, NID_basic_constraints, "critical,CA:FALSE");
      add_ext(x509, NID_ext_key_usage, "serverAuth,clientAuth");
      add_ext(x509, NID_subject_key_identifier, "hash");
      add_ext(x509, NID_netscape_cert_type, "server");
      add_ext(x509, NID_netscape_comment, "Generated from murmur");
      
      X509_sign(x509, pkey, EVP_md5());

      crt.resize(i2d_X509(x509, NULL));
      unsigned char *dptr=reinterpret_cast<unsigned char *>(crt.data());
      i2d_X509(x509, &dptr);
      
      qscCert = QSslCertificate(crt, QSsl::Der);
      if (qscCert.isNull())
        qFatal("Certificate generation failed");
        
      key.resize(i2d_PrivateKey(pkey, NULL));
      dptr=reinterpret_cast<unsigned char *>(key.data());
      i2d_PrivateKey(pkey, &dptr);
      
      qskKey = QSslKey(key, QSsl::Rsa, QSsl::Der);
      if (qskKey.isNull())
        qFatal("Key generation failed");

      QFile pemout(g_sp.qsSSLStore);
      if (! pemout.open(QIODevice::WriteOnly)) {
        qFatal("Failed to open keystore %s for writing.", qPrintable(g_sp.qsSSLStore));
      }
      pemout.write(qscCert.toPem());
      pemout.write(qskKey.toPem());
      pemout.close();
    }
  }

  QList<QSslCipher> pref;
  foreach(QSslCipher c, QSslSocket::defaultCiphers()) {
    if (c.usedBits() < 128)
      continue;
    pref << c;
  }
  if (pref.isEmpty())
    qFatal("No ciphers of at least 128 bit found");
  QSslSocket::setDefaultCiphers(pref);
}

const QSslCertificate &Cert::getCert() const {
  return qscCert;
}

const QSslKey &Cert::getKey() const {
  return qskKey;
}

const QString Cert::getDigest() const {
  return QString::fromLatin1(qscCert.digest().toHex());
}
