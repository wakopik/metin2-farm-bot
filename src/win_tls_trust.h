#pragma once
#include <httplib.h>

// httplib::SSLClient (OpenSSL tabanli) VARSAYILAN olarak Windows'un KENDI
// sertifika deposunu (CryptoAPI - tarayicilarin/diger Windows programlarinin
// guvendigi ayni depo) KULLANMAZ. Bu yuzden gecerli, herkese acik bir TLS
// sunucusuna (ör. keyauth.win, discord.com) baglanirken bile "SSL server
// verification failed" hatasi alinir - OpenSSL'in kendi ayri, bos bir
// guven deposu vardir.
//
// Bu fonksiyon, Windows'un KENDI kok sertifika deposunu (ROOT) okuyup
// istemcinin OpenSSL guven deposuna ekler - boylece Windows'un guvendigi
// HER sertifikaya (normal bir tarayici gibi) guvenilir.
//
// BILINCLI TASARIM KARARI: dogrulamayi TAMAMEN KAPATMAK
// (enable_server_certificate_verification(false)) YERINE bu yontem
// tercih edildi - dogrulamayi kapatmak, TAM olarak korumaya calistigimiz
// sey olan agdaki-adam (MITM) saldirisina karsi HICBIR koruma birakmazdi
// (bkz. keyauth_client.h - lisans dogrulamasinin GUVENLIGI TLS'e dayanir).
//
// Basarisiz olursa (ör. sertifika deposu acilamazsa) sessizce hicbir sey
// yapmaz - istemci OpenSSL'in varsayilan (bos) guven deposuyla kalir ve
// baglanti yine basarisiz olur, ama BASKA bir hataya sebep olmaz.
void ConfigureSystemTrustStore(httplib::SSLClient& client);
