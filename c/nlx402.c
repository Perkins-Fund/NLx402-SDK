#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>


typedef struct {
    int ok;
    double created_at;
    char *wallet_id;
    char *selected_mint;
} AuthMeResponse;

typedef struct {
    int ok;
    char *network;
    char **supported_chains;
    int supported_chains_count;
    char *version;
    char **supported_mints;
    int supported_mints_count;
} MetadataResponse;

typedef struct {
    char *amount;
    char *chain;
    int decimals;
    double expires_at;
    char *mint;
    char *network;
    char *nonce;
    char *recipient;
    char *version;
} QuoteResponse;

typedef struct {
    int ok;
} VerifyResponse;

typedef struct {
    int ok;
    char *amount;
    int decimals;
    char *mint;
    char *nonce;
    char *status;
    char *tx;
    char *version;
} PaidAccessResponse;

typedef struct {
    char *base_url;
    char *api_key;
} Nlx402Client;


typedef struct {
    char *data;
    size_t size;
} MemoryChunk;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    MemoryChunk *mem = (MemoryChunk *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (ptr == NULL) {
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = '\0';

    return realsize;
}

static char *dup_string(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

void nlx402_client_init(Nlx402Client *client, const char *base_url, const char *api_key) {
    client->base_url = dup_string(base_url ? base_url : "https://pay.thrt.ai");
    client->api_key  = api_key ? dup_string(api_key) : NULL;

    size_t len = strlen(client->base_url);
    while (len > 0 && client->base_url[len - 1] == '/') {
        client->base_url[len - 1] = '\0';
        len--;
    }
}

void nlx402_client_set_api_key(Nlx402Client *client, const char *api_key) {
    if (client->api_key) free(client->api_key);
    client->api_key = api_key ? dup_string(api_key) : NULL;
}

void nlx402_client_cleanup(Nlx402Client *client) {
    if (client->base_url) free(client->base_url);
    if (client->api_key) free(client->api_key);
}

int nlx402_request(
    Nlx402Client *client,
    const char *path,
    const char *method,
    int require_api_key,
    struct curl_slist *extra_headers,
    const char *body,
    long *out_status,
    MemoryChunk *out_chunk
) {
    CURL *curl = NULL;
    CURLcode res;
    int retval = -1;

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl_easy_init failed\n");
        return -1;
    }

    size_t url_len = strlen(client->base_url) + strlen(path) + 1;
    char *url = (char *)malloc(url_len);
    if (!url) {
        curl_easy_cleanup(curl);
        return -1;
    }
    snprintf(url, url_len, "%s%s", client->base_url, path);

    MemoryChunk chunk;
    chunk.data = malloc(1);
    chunk.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        }
    } else {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        }
    }

    struct curl_slist *headers = NULL;
    if (require_api_key) {
        if (!client->api_key) {
            fprintf(stderr, "NLx402: API key is required but not set.\n");
            free(url);
            free(chunk.data);
            curl_easy_cleanup(curl);
            return -1;
        }
        char api_header[256];
        snprintf(api_header, sizeof(api_header), "x-api-key: %s", client->api_key);
        headers = curl_slist_append(headers, api_header);
    }

    if (extra_headers) {
        struct curl_slist *tmp = extra_headers;
        while (tmp) {
            headers = curl_slist_append(headers, tmp->data);
            tmp = tmp->next;
        }
    }

    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        retval = -1;
        goto cleanup;
    }

    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    if (out_status) *out_status = status_code;

    if (status_code < 200 || status_code >= 300) {
        fprintf(stderr, "NLx402 request failed with status %ld, body: %s\n",
                status_code, chunk.data ? chunk.data : "");
        retval = -1;
        goto cleanup;
    }

    if (out_chunk) {
        out_chunk->data = chunk.data;
        out_chunk->size = chunk.size;
        chunk.data = NULL;
    } else {
        free(chunk.data);
    }

    retval = 0;

cleanup:
    if (headers) curl_slist_free_all(headers);
    if (chunk.data) free(chunk.data);
    free(url);
    curl_easy_cleanup(curl);
    return retval;
}


int nlx402_get_metadata(Nlx402Client *client, MetadataResponse *out) {
    long status;
    MemoryChunk chunk = {0};
    int rc = nlx402_request(client, "/api/metadata", "GET", 0, NULL, NULL, &status, &chunk);
    if (rc != 0) return rc;

    cJSON *root = cJSON_Parse(chunk.data);
    if (!root) {
        fprintf(stderr, "Failed to parse JSON from /api/metadata\n");
        free(chunk.data);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->ok = cJSON_IsTrue(cJSON_GetObjectItem(root, "ok"));

    cJSON *metadata = cJSON_GetObjectItem(root, "metadata");
    if (metadata && cJSON_IsObject(metadata)) {
        cJSON *network = cJSON_GetObjectItem(metadata, "network");
        cJSON *version = cJSON_GetObjectItem(metadata, "version");
        cJSON *chains  = cJSON_GetObjectItem(metadata, "supported_chains");

        if (cJSON_IsString(network)) out->network = dup_string(network->valuestring);
        if (cJSON_IsString(version)) out->version = dup_string(version->valuestring);

        if (cJSON_IsArray(chains)) {
            int count = cJSON_GetArraySize(chains);
            out->supported_chains = (char **)calloc(count, sizeof(char *));
            out->supported_chains_count = count;
            for (int i = 0; i < count; i++) {
                cJSON *item = cJSON_GetArrayItem(chains, i);
                if (cJSON_IsString(item)) {
                    out->supported_chains[i] = dup_string(item->valuestring);
                }
            }
        }
    }

    cJSON *mints = cJSON_GetObjectItem(root, "supported_mints");
    if (cJSON_IsArray(mints)) {
        int count = cJSON_GetArraySize(mints);
        out->supported_mints = (char **)calloc(count, sizeof(char *));
        out->supported_mints_count = count;
        for (int i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(mints, i);
            if (cJSON_IsString(item)) {
                out->supported_mints[i] = dup_string(item->valuestring);
            }
        }
    }

    cJSON_Delete(root);
    free(chunk.data);
    return 0;
}

void nlx402_free_metadata(MetadataResponse *m) {
    if (!m) return;
    if (m->network) free(m->network);
    if (m->version) free(m->version);
    for (int i = 0; i < m->supported_chains_count; i++) {
        if (m->supported_chains && m->supported_chains[i])
            free(m->supported_chains[i]);
    }
    if (m->supported_chains) free(m->supported_chains);

    for (int i = 0; i < m->supported_mints_count; i++) {
        if (m->supported_mints && m->supported_mints[i])
            free(m->supported_mints[i]);
    }
    if (m->supported_mints) free(m->supported_mints);
}


int nlx402_get_auth_me(Nlx402Client *client, AuthMeResponse *out) {
    long status;
    MemoryChunk chunk = {0};
    int rc = nlx402_request(client, "/api/auth/me", "GET", 1, NULL, NULL, &status, &chunk);
    if (rc != 0) return rc;

    cJSON *root = cJSON_Parse(chunk.data);
    if (!root) {
        fprintf(stderr, "Failed to parse JSON from /api/auth/me\n");
        free(chunk.data);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->ok = cJSON_IsTrue(cJSON_GetObjectItem(root, "ok"));

    cJSON *created_at = cJSON_GetObjectItem(root, "created_at");
    if (cJSON_IsNumber(created_at)) out->created_at = created_at->valuedouble;

    cJSON *wallet_id = cJSON_GetObjectItem(root, "wallet_id");
    cJSON *selected_mint = cJSON_GetObjectItem(root, "selected_mint");
    if (cJSON_IsString(wallet_id)) out->wallet_id = dup_string(wallet_id->valuestring);
    if (cJSON_IsString(selected_mint)) out->selected_mint = dup_string(selected_mint->valuestring);

    cJSON_Delete(root);
    free(chunk.data);
    return 0;
}

void nlx402_free_auth_me(AuthMeResponse *r) {
    if (!r) return;
    if (r->wallet_id) free(r->wallet_id);
    if (r->selected_mint) free(r->selected_mint);
}


int nlx402_get_quote(Nlx402Client *client, double total_price, QuoteResponse *out) {
    long status;
    MemoryChunk chunk = {0};

    if (total_price <= 0.0) total_price = 0.5;

    char header_buf[128];
    snprintf(header_buf, sizeof(header_buf), "x-total-price: %.8f", total_price);

    struct curl_slist extra;
    extra.data = header_buf;
    extra.next = NULL;

    int rc = nlx402_request(client, "/protected", "GET", 1, &extra, NULL, &status, &chunk);
    if (rc != 0) return rc;

    cJSON *root = cJSON_Parse(chunk.data);
    if (!root) {
        fprintf(stderr, "Failed to parse JSON from /protected (quote)\n");
        free(chunk.data);
        return -1;
    }

    memset(out, 0, sizeof(*out));

    cJSON *amount = cJSON_GetObjectItem(root, "amount");
    cJSON *chain  = cJSON_GetObjectItem(root, "chain");
    cJSON *decimals = cJSON_GetObjectItem(root, "decimals");
    cJSON *expires_at = cJSON_GetObjectItem(root, "expires_at");
    cJSON *mint   = cJSON_GetObjectItem(root, "mint");
    cJSON *network= cJSON_GetObjectItem(root, "network");
    cJSON *nonce  = cJSON_GetObjectItem(root, "nonce");
    cJSON *recipient = cJSON_GetObjectItem(root, "recipient");
    cJSON *version  = cJSON_GetObjectItem(root, "version");

    if (cJSON_IsString(amount)) out->amount = dup_string(amount->valuestring);
    if (cJSON_IsString(chain)) out->chain = dup_string(chain->valuestring);
    if (cJSON_IsNumber(decimals)) out->decimals = decimals->valueint;
    if (cJSON_IsNumber(expires_at)) out->expires_at = expires_at->valuedouble;
    if (cJSON_IsString(mint)) out->mint = dup_string(mint->valuestring);
    if (cJSON_IsString(network)) out->network = dup_string(network->valuestring);
    if (cJSON_IsString(nonce)) out->nonce = dup_string(nonce->valuestring);
    if (cJSON_IsString(recipient)) out->recipient = dup_string(recipient->valuestring);
    if (cJSON_IsString(version)) out->version = dup_string(version->valuestring);

    cJSON_Delete(root);
    free(chunk.data);
    return 0;
}

void nlx402_free_quote(QuoteResponse *q) {
    if (!q) return;
    if (q->amount) free(q->amount);
    if (q->chain) free(q->chain);
    if (q->mint) free(q->mint);
    if (q->network) free(q->network);
    if (q->nonce) free(q->nonce);
    if (q->recipient) free(q->recipient);
    if (q->version) free(q->version);
}


int nlx402_verify_quote(Nlx402Client *client, const QuoteResponse *quote, const char *nonce, VerifyResponse *out) {
    if (!nonce || !quote || !quote->nonce) {
        fprintf(stderr, "verify_quote: nonce and quote are required\n");
        return -1;
    }

    cJSON *q = cJSON_CreateObject();
    if (!q) return -1;
    cJSON_AddStringToObject(q, "amount", quote->amount ? quote->amount : "");
    cJSON_AddStringToObject(q, "chain", quote->chain ? quote->chain : "");
    cJSON_AddNumberToObject(q, "decimals", quote->decimals);
    cJSON_AddNumberToObject(q, "expires_at", quote->expires_at);
    cJSON_AddStringToObject(q, "mint", quote->mint ? quote->mint : "");
    cJSON_AddStringToObject(q, "network", quote->network ? quote->network : "");
    cJSON_AddStringToObject(q, "nonce", quote->nonce ? quote->nonce : "");
    cJSON_AddStringToObject(q, "recipient", quote->recipient ? quote->recipient : "");
    cJSON_AddStringToObject(q, "version", quote->version ? quote->version : "");

    char *quote_str = cJSON_PrintUnformatted(q);
    cJSON_Delete(q);
    if (!quote_str) return -1;

    size_t body_len = strlen("payment_data=") + strlen(quote_str) + strlen("&nonce=") + strlen(nonce) + 1;
    char *body = (char *)malloc(body_len);
    if (!body) {
        free(quote_str);
        return -1;
    }
    snprintf(body, body_len, "payment_data=%s&nonce=%s", quote_str, nonce);

    struct curl_slist extra_headers;
    extra_headers.data = "Content-Type: application/x-www-form-urlencoded";
    extra_headers.next = NULL;

    long status;
    MemoryChunk chunk = {0};
    int rc = nlx402_request(client, "/verify", "POST", 1, &extra_headers, body, &status, &chunk);

    free(quote_str);
    free(body);

    if (rc != 0) return rc;

    cJSON *root = cJSON_Parse(chunk.data);
    if (!root) {
        fprintf(stderr, "Failed to parse JSON from /verify\n");
        free(chunk.data);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->ok = cJSON_IsTrue(cJSON_GetObjectItem(root, "ok"));

    cJSON_Delete(root);
    free(chunk.data);
    return 0;
}


int nlx402_get_paid_access(Nlx402Client *client, const char *tx, const char *nonce, PaidAccessResponse *out) {
    if (!tx || !nonce) {
        fprintf(stderr, "get_paid_access: tx and nonce are required\n");
        return -1;
    }

    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "tx", tx);
    cJSON_AddStringToObject(p, "nonce", nonce);
    char *payment_str = cJSON_PrintUnformatted(p);
    cJSON_Delete(p);
    if (!payment_str) return -1;

    size_t header_len = strlen("x-payment: ") + strlen(payment_str) + 1;
    char *header_buf = (char *)malloc(header_len);
    if (!header_buf) {
        free(payment_str);
        return -1;
    }
    snprintf(header_buf, header_len, "x-payment: %s", payment_str);

    struct curl_slist extra_headers;
    extra_headers.data = header_buf;
    extra_headers.next = NULL;

    long status;
    MemoryChunk chunk = {0};
    int rc = nlx402_request(client, "/protected", "GET", 1, &extra_headers, NULL, &status, &chunk);

    free(payment_str);
    free(header_buf);

    if (rc != 0) return rc;

    cJSON *root = cJSON_Parse(chunk.data);
    if (!root) {
        fprintf(stderr, "Failed to parse JSON from /protected (paid)\n");
        free(chunk.data);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->ok = cJSON_IsTrue(cJSON_GetObjectItem(root, "ok"));

    cJSON *x402 = cJSON_GetObjectItem(root, "x402");
    if (x402 && cJSON_IsObject(x402)) {
        cJSON *amount = cJSON_GetObjectItem(x402, "amount");
        cJSON *decimals = cJSON_GetObjectItem(x402, "decimals");
        cJSON *mint = cJSON_GetObjectItem(x402, "mint");
        cJSON *nonce_j = cJSON_GetObjectItem(x402, "nonce");
        cJSON *status_j = cJSON_GetObjectItem(x402, "status");
        cJSON *tx_j = cJSON_GetObjectItem(x402, "tx");
        cJSON *version = cJSON_GetObjectItem(x402, "version");

        if (cJSON_IsString(amount)) out->amount = dup_string(amount->valuestring);
        if (cJSON_IsNumber(decimals)) out->decimals = decimals->valueint;
        if (cJSON_IsString(mint)) out->mint = dup_string(mint->valuestring);
        if (cJSON_IsString(nonce_j)) out->nonce = dup_string(nonce_j->valuestring);
        if (cJSON_IsString(status_j)) out->status = dup_string(status_j->valuestring);
        if (cJSON_IsString(tx_j)) out->tx = dup_string(tx_j->valuestring);
        if (cJSON_IsString(version)) out->version = dup_string(version->valuestring);
    }

    cJSON_Delete(root);
    free(chunk.data);
    return 0;
}

void nlx402_free_paid_access(PaidAccessResponse *p) {
    if (!p) return;
    if (p->amount) free(p->amount);
    if (p->mint) free(p->mint);
    if (p->nonce) free(p->nonce);
    if (p->status) free(p->status);
    if (p->tx) free(p->tx);
    if (p->version) free(p->version);
}

int nlx402_get_and_verify_quote(
    Nlx402Client *client,
    double total_price,
    QuoteResponse *out_quote,
    VerifyResponse *out_verify
) {
    int rc = nlx402_get_quote(client, total_price, out_quote);
    if (rc != 0) return rc;

    rc = nlx402_verify_quote(client, out_quote, out_quote->nonce, out_verify);
    return rc;
}

