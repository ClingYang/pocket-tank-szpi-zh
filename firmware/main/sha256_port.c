#include "sha256_port.h"
#include "esp_idf_version.h"

#if ESP_IDF_VERSION_MAJOR >= 6
#include "psa/crypto.h"
#else
#include "mbedtls/sha256.h"
#endif

bool sha256_port(const void *data, size_t length, uint8_t digest[32]) {
#if ESP_IDF_VERSION_MAJOR >= 6
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    psa_status_t status = psa_crypto_init();
    if (status == PSA_SUCCESS) status = psa_hash_setup(&operation, PSA_ALG_SHA_256);
    for (size_t offset = 0; offset < length && status == PSA_SUCCESS; offset += 65536) {
        size_t chunk = length - offset;
        if (chunk > 65536) chunk = 65536;
        status = psa_hash_update(&operation, (const uint8_t *)data + offset, chunk);
    }
    size_t digest_length = 0;
    if (status == PSA_SUCCESS) status = psa_hash_finish(&operation, digest, 32, &digest_length);
    psa_hash_abort(&operation);
    return status == PSA_SUCCESS && digest_length == 32;
#else
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    int status = mbedtls_sha256_starts(&context, 0);
    for (size_t offset = 0; offset < length && status == 0; offset += 65536) {
        size_t chunk = length - offset;
        if (chunk > 65536) chunk = 65536;
        status = mbedtls_sha256_update(&context, (const uint8_t *)data + offset, chunk);
    }
    if (status == 0) status = mbedtls_sha256_finish(&context, digest);
    mbedtls_sha256_free(&context);
    return status == 0;
#endif
}
