# NLx402 Rust SDK

### Example flow

```rust
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let client = Nlx402Client::with_api_key("your-api-key-here");

    // Introspect current key
    let me = client.get_auth_me().await?;
    println!("Wallet: {}", me.wallet_id);

    // Get metadata
    let meta = client.get_metadata().await?;
    println!("Network: {}", meta.metadata.network);

    // Full flow: get + verify quote
    let result = client.get_and_verify_quote(None).await?; // defaults total_price to 0.5
    println!("Quote nonce: {}", result.quote.nonce);
    println!("Verify ok: {}", result.verify.ok);

    // After on-chain payment:
    // let paid = client.get_paid_access("solana-tx-sig", &result.quote.nonce).await?;
    // println!("Paid access status: {}", paid.x402.status);

    Ok(())
}
```