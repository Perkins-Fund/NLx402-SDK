# NLx402 C SDK

### Example flow
```
int main(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    Nlx402Client client;
    nlx402_client_init(&client, "https://pay.thrt.ai", "YOUR_API_KEY_HERE");

    printf("=== NLx402 Example Flow (C) ===\n");

    AuthMeResponse me;
    if (nlx402_get_auth_me(&client, &me) == 0 && me.ok) {
        printf("AuthMe OK:\n");
        printf("  wallet_id: %s\n", me.wallet_id);
        printf("  selected_mint: %s\n", me.selected_mint);
    } else {
        printf("AuthMe failed\n");
    }
    nlx402_free_auth_me(&me);
    MetadataResponse meta;
    if (nlx402_get_metadata(&client, &meta) == 0) {
        printf("\nMetadata:\n");
        printf("  network: %s\n", meta.network);
        printf("  version: %s\n", meta.version);
        printf("  supported_chains: ");
        for (int i = 0; i < meta.supported_chains_count; i++) {
            printf("%s%s", meta.supported_chains[i],
                   (i + 1 < meta.supported_chains_count) ? ", " : "");
        }
        printf("\n");
    } else {
        printf("Failed to get metadata\n");
    }
    nlx402_free_metadata(&meta);

    QuoteResponse quote;
    VerifyResponse verify;
    if (nlx402_get_and_verify_quote(&client, 0.5, &quote, &verify) == 0 && verify.ok) {
        /* example flow */
        printf("\nQuote + Verify:\n");
        printf("  amount: %s\n", quote.amount);
        printf("  mint:   %s\n", quote.mint);
        printf("  nonce:  %s\n", quote.nonce);
        printf("  recipient: %s\n", quote.recipient);
        printf("  expires_at: %.0f\n", quote.expires_at);

        printf("\nNow you would:\n");
        printf("  - Pass this quote to the client\n");
        printf("  - Pay on-chain (mint %s to recipient %s)\n", quote.mint, quote.recipient);
        printf("  - Wait for a confirmed transaction signature\n");
    } else {
        printf("Quote or verify failed\n");
        nlx402_free_quote(&quote);
        nlx402_client_cleanup(&client);
        curl_global_cleanup();
        return 1;
    }

    const char *example_tx_sig = "REPLACE_WITH_REAL_SOLANA_TX_SIGNATURE";

    PaidAccessResponse paid;
    if (nlx402_get_paid_access(&client, example_tx_sig, quote.nonce, &paid) == 0 && paid.ok) {
        printf("\nPaid access OK:\n");
        printf("  status: %s\n", paid.status);
        printf("  tx:     %s\n", paid.tx);
        printf("  amount: %s (decimals=%d)\n", paid.amount, paid.decimals);
    } else {
        printf("\nPaid access failed (this will fail until you use a real tx signature).\n");
    }

    nlx402_free_paid_access(&paid);
    nlx402_free_quote(&quote);

    nlx402_client_cleanup(&client);
    curl_global_cleanup();
    return 0;
}
```
