#include "win_tls_trust.h"

#include <windows.h>
#include <wincrypt.h>

#include <openssl/x509.h>

void ConfigureSystemTrustStore(httplib::SSLClient& client) {
    HCERTSTORE hStore = CertOpenSystemStoreA(0, "ROOT");
    if (!hStore) return;

    X509_STORE* store = X509_STORE_new();
    if (!store) {
        CertCloseStore(hStore, 0);
        return;
    }

    PCCERT_CONTEXT ctx = nullptr;
    while ((ctx = CertEnumCertificatesInStore(hStore, ctx)) != nullptr) {
        const unsigned char* encoded = ctx->pbCertEncoded;
        X509* x509 = d2i_X509(nullptr, &encoded, ctx->cbCertEncoded);
        if (x509) {
            X509_STORE_add_cert(store, x509);
            X509_free(x509);
        }
    }
    CertCloseStore(hStore, 0);

    // httplib set_ca_cert_store MULKIYETI DEVRALIR (kendi serbest birakir) -
    // burada X509_STORE_free CAGRILMAZ.
    client.set_ca_cert_store(static_cast<httplib::tls::ca_store_t>(store));
}
