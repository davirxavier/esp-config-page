//
// Created by xav on 2/17/26.
//

#ifndef ESP_CONFIG_PAGE_CERT_H
#define ESP_CONFIG_PAGE_CERT_H
#ifdef ESP_CONP_HTTPS_SERVER

#include "mbedtls/pk.h"
#include "mbedtls/ecp.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"

#ifndef ESP_CONP_CERT_STORAGE_CERT
#define ESP_CONP_CERT_STORAGE_CERT "conp_cert"
#endif

#ifndef ESP_CONP_CERT_STORAGE_KEY
#define ESP_CONP_CERT_STORAGE_KEY "conp_key"
#endif

namespace ESP_CONFIG_PAGE_CERT
{
    inline size_t certLen = 0;
    inline size_t keyLen = 0;
    inline char certBuffer[1024]{};
    inline char keyBuffer[512]{};

    inline void logError(const char* name, int res)
    {
        if (res != 0)
        {
            LOGF("Error in %s: 0x%x\n", name, -res);
        }
    }

    inline void generateCertificate(
        ESP_CONFIG_PAGE::KeyValueStorage* storage,
        const char* persName,
        const char* commonName,
        const char* org,
        const char* country)
    {
        LOGN("Generating keys.");
        // Public/Private key generation
        mbedtls_pk_context key;
        mbedtls_pk_init(&key);

        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_entropy_context entropy;

        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr_drbg);

        mbedtls_ctr_drbg_seed(
            &ctr_drbg,
            mbedtls_entropy_func,
            &entropy,
            (const unsigned char*)persName,
            strlen(persName));

        logError("key generation setup", mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)));
        logError("key generation", mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), mbedtls_ctr_drbg_random, &ctr_drbg));

        LOGN("Generating certificate.");
        // Cert
        mbedtls_x509write_cert crt;
        mbedtls_x509write_crt_init(&crt);

        mbedtls_x509write_crt_set_subject_key(&crt, &key);
        mbedtls_x509write_crt_set_issuer_key(&crt, &key);
        mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);

        char nameBuf[32 + strlen(commonName) + strlen(org) + strlen(country)]{};
        snprintf(nameBuf, sizeof(nameBuf), "CN=%s,O=%s,C=%s", commonName, org, country);

        mbedtls_x509write_crt_set_subject_name(&crt, nameBuf);
        mbedtls_x509write_crt_set_issuer_name(&crt, nameBuf);
        mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);

        unsigned char serialBuf[min(MBEDTLS_X509_RFC5280_MAX_SERIAL_LEN, 32)]{};
        logError("serial generation", mbedtls_ctr_drbg_random(&ctr_drbg, serialBuf, sizeof(serialBuf)));
        serialBuf[0] &= 0x7F;
        logError("certificate serial definition",
                 mbedtls_x509write_crt_set_serial_raw(&crt, serialBuf, sizeof(serialBuf)));
        logError("certificate validity", mbedtls_x509write_crt_set_validity(&crt, "20260101000000", "20560101000000"));
        // TODO add regeneration when expired

        // write
        int res = mbedtls_x509write_crt_pem(&crt, (unsigned char*)certBuffer, sizeof(certBuffer),
                                            mbedtls_ctr_drbg_random, &ctr_drbg);
        if (res == 0)
        {
            storage->save(ESP_CONP_CERT_STORAGE_CERT, certBuffer);
            certLen = strlen(certBuffer);
            LOGN("Certificate generated and saved successfully.");
        }
        else
        {
            LOGF("Error generating certificate PEM file: %d\n", res);
        }

        res = mbedtls_pk_write_key_pem(&key, (unsigned char*)keyBuffer, sizeof(keyBuffer));
        if (res == 0)
        {
            storage->save(ESP_CONP_CERT_STORAGE_KEY, (const char*)keyBuffer);
            keyLen = strlen(keyBuffer);
            LOGN("Key generated and saved successfully.");
        }
        else
        {
            LOGF("Error generating key PEM file: %d\n", res);
        }

        mbedtls_pk_free(&key);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_x509write_crt_free(&crt);
    }

    inline void printFingerprint(const char *pem_cert)
    {
        mbedtls_x509_crt cert;
        mbedtls_x509_crt_init(&cert);

        int ret = mbedtls_x509_crt_parse(&cert, (const unsigned char *) pem_cert, strlen(pem_cert) + 1);
        if (ret != 0) {
            Serial.printf("Failed to parse cert: -0x%x", -ret);
            return;
        }

        unsigned char hash[32];
        mbedtls_sha256(cert.raw.p, cert.raw.len, hash, 0);

        Serial.println("HTTPS certificate SHA256 Fingerprint:");
        char buf[3];
        for (int i = 0; i < 32; i++) {
            sprintf(buf, "%02X", hash[i]);
            printf("%s", buf);
            if (i < 31) printf(":");
        }
        printf("\n");

        mbedtls_x509_crt_free(&cert);
    }

    inline void initModule(
        ESP_CONFIG_PAGE::KeyValueStorage* storage,
        const char* persName = "esp32_tls",
        const char* commonName = "esp32.local",
        const char* org = "ESP32_LOCAL",
        const char* country = "US")
    {
        LOGN("Initializing certificate module.");
        if (storage == nullptr)
        {
            return;
        }

        if (storage->exists(ESP_CONP_CERT_STORAGE_KEY) && storage->exists(ESP_CONP_CERT_STORAGE_CERT))
        {
            storage->recover(ESP_CONP_CERT_STORAGE_KEY, keyBuffer, sizeof(keyBuffer));
            storage->recover(ESP_CONP_CERT_STORAGE_CERT, certBuffer, sizeof(certBuffer));

            keyLen = strlen(keyBuffer);
            certLen = strlen(certBuffer);

            LOGF("Recovered certificate and key from storage, key len is %zu, cert len is %zu\n", keyLen, certLen);
            printFingerprint(certBuffer);
        }
        else
        {
            LOGN("Certificate does not exist, generating...");
            generateCertificate(storage, persName, commonName, org, country);
        }

        LOGN("Certificate initialized.");
    }
}

#endif
#endif //ESP_CONFIG_PAGE_CERT_H
