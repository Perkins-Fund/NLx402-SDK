use reqwest::{Client, StatusCode};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::fmt;

#[derive(Debug)]
pub enum Nlx402Error {r
    Http(reqwest::Error),
    Api {
        status: u16,
        body: Option<Value>,
    },
    MissingApiKey,
    InvalidResponse(String),
}

impl fmt::Display for Nlx402Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Nlx402Error::Http(err) => write!(f, "HTTP error: {}", err),
            Nlx402Error::Api { status, .. } => {
                write!(f, "NLx402 request failed with status {}", status)
            }
            Nlx402Error::MissingApiKey => {
                write!(f, "NLx402: API key is required but not set.")
            }
            Nlx402Error::InvalidResponse(msg) => write!(f, "Invalid response: {}", msg),
        }
    }
}

impl std::error::Error for Nlx402Error {}

impl From<reqwest::Error> for Nlx402Error {
    fn from(err: reqwest::Error) -> Self {
        Nlx402Error::Http(err)
    }
}

/// AuthMeResponse (GET /api/auth/me)
#[derive(Debug, Deserialize, Serialize)]
pub struct AuthMeResponse {
    pub ok: bool,
    pub created_at: f64,
    pub wallet_id: String,
    pub selected_mint: String,
}

/// Metadata nested object
#[derive(Debug, Deserialize, Serialize)]
pub struct MetadataInfo {
    pub network: String,
    pub supported_chains: Vec<String>,
    pub version: String,
}

/// MetadataResponse (GET /api/metadata)
#[derive(Debug, Deserialize, Serialize)]
pub struct MetadataResponse {
    pub ok: bool,
    pub metadata: MetadataInfo,
    pub supported_mints: Vec<String>,
}

/// QuoteResponse (GET /protected step 1)
#[derive(Debug, Deserialize, Serialize, Clone)]
pub struct QuoteResponse {
    pub amount: String,
    pub chain: String,
    pub decimals: u32,
    pub expires_at: i64,
    pub mint: String,
    pub network: String,
    pub nonce: String,
    pub recipient: String,
    pub version: String,
}

/// VerifyResponse (POST /verify)
#[derive(Debug, Deserialize, Serialize)]
pub struct VerifyResponse {
    pub ok: bool,
}

/// Paid access nested x402 object
#[derive(Debug, Deserialize, Serialize)]
pub struct X402Info {
    pub amount: String,
    pub decimals: u32,
    pub mint: String,
    pub nonce: String,
    pub status: String,
    pub tx: String,
    pub version: String,
}

/// PaidAccessResponse (GET /protected with X-Payment)
#[derive(Debug, Deserialize, Serialize)]
pub struct PaidAccessResponse {
    pub ok: bool,
    pub x402: X402Info,
}

/// Convenience return type for get_and_verify_quote
#[derive(Debug)]
pub struct QuoteAndVerify {
    pub quote: QuoteResponse,
    pub verify: VerifyResponse,
}

/// Options-like struct for constructing the client.
pub struct Nlx402ClientOptions {
    pub api_key: Option<String>,
    pub base_url: Option<String>,
}

/// NLx402 Rust client (async).
pub struct Nlx402Client {
    base_url: String,
    api_key: Option<String>,
    http: Client,
}

impl Nlx402Client {
    pub fn new(options: Nlx402ClientOptions) -> Self {
        let base_url = options
            .base_url
            .unwrap_or_else(|| "https://pay.thrt.ai".to_string());
        let base_url = base_url.trim_end_matches('/').to_string();

        Nlx402Client {
            base_url,
            api_key: options.api_key,
            http: Client::new(),
        }
    }

    pub fn with_api_key(api_key: impl Into<String>) -> Self {
        Self::new(Nlx402ClientOptions {
            api_key: Some(api_key.into()),
            base_url: None,
        })
    }

    pub fn set_api_key(&mut self, api_key: impl Into<String>) {
        self.api_key = Some(api_key.into());
    }

    fn require_api_key(&self) -> Result<&str, Nlx402Error> {
        self.api_key
            .as_deref()
            .ok_or(Nlx402Error::MissingApiKey)
    }

    async fn send_json<T>(&self, builder: reqwest::RequestBuilder) -> Result<T, Nlx402Error>
    where
        T: for<'de> serde::Deserialize<'de>,
    {
        let res = builder.send().await?;
        let status = res.status();
        let text = res.text().await?;

        if !status.is_success() {
            let body = serde_json::from_str::<Value>(&text).ok();
            return Err(Nlx402Error::Api {
                status: status.as_u16(),
                body,
            });
        }

        serde_json::from_str::<T>(&text)
            .map_err(|e| Nlx402Error::InvalidResponse(e.to_string()))
    }

    pub async fn get_auth_me(&self) -> Result<AuthMeResponse, Nlx402Error> {
        let api_key = self.require_api_key()?;
        let url = format!("{}/api/auth/me", self.base_url);

        let builder = self
            .http
            .get(&url)
            .header("x-api-key", api_key);

        self.send_json(builder).await
    }

    pub async fn get_metadata(&self) -> Result<MetadataResponse, Nlx402Error> {
        let url = format!("{}/api/metadata", self.base_url);
        let builder = self.http.get(&url);
        self.send_json(builder).await
    }

    pub async fn get_quote(
        &self,
        total_price: Option<impl Into<String>>,
    ) -> Result<QuoteResponse, Nlx402Error> {
        let api_key = self.require_api_key()?;
        let url = format!("{}/protected", self.base_url);

        let total = total_price
            .map(|t| t.into())
            .unwrap_or_else(|| "0.5".to_string());

        let builder = self
            .http
            .get(&url)
            .header("x-api-key", api_key)
            .header("x-total-price", total);

        self.send_json(builder).await
    }

    pub async fn verify_quote(
        &self,
        quote: &QuoteResponse,
        nonce: &str,
    ) -> Result<VerifyResponse, Nlx402Error> {
        let api_key = self.require_api_key()?;
        if nonce.is_empty() {
            return Err(Nlx402Error::InvalidResponse(
                "verify_quote: nonce is required".into(),
            ));
        }

        let url = format!("{}/verify", self.base_url);
        let payment_data =
            serde_json::to_string(quote).map_err(|e| Nlx402Error::InvalidResponse(e.to_string()))?;

        let form = [("payment_data", payment_data), ("nonce", nonce.to_string())];

        let builder = self
            .http
            .post(&url)
            .header("x-api-key", api_key)
            .form(&form);

        self.send_json(builder).await
    }

    pub async fn verify_quote_raw(
        &self,
        quote_json: &str,
        nonce: &str,
    ) -> Result<VerifyResponse, Nlx402Error> {
        let api_key = self.require_api_key()?;
        if nonce.is_empty() {
            return Err(Nlx402Error::InvalidResponse(
                "verify_quote_raw: nonce is required".into(),
            ));
        }

        let url = format!("{}/verify", self.base_url);
        let form = [("payment_data", quote_json.to_string()), ("nonce", nonce.to_string())];

        let builder = self
            .http
            .post(&url)
            .header("x-api-key", api_key)
            .form(&form);

        self.send_json(builder).await
    }

    pub async fn get_paid_access(
        &self,
        tx: &str,
        nonce: &str,
    ) -> Result<PaidAccessResponse, Nlx402Error> {
        let api_key = self.require_api_key()?;
        if tx.is_empty() || nonce.is_empty() {
            return Err(Nlx402Error::InvalidResponse(
                "get_paid_access: tx and nonce are required".into(),
            ));
        }

        let url = format!("{}/protected", self.base_url);
        let x_payment = serde_json::json!({ "tx": tx, "nonce": nonce }).to_string();

        let builder = self
            .http
            .get(&url)
            .header("x-api-key", api_key)
            .header("x-payment", x_payment);

        self.send_json(builder).await
    }

    pub async fn get_and_verify_quote(
        &self,
        total_price: Option<impl Into<String>>,
    ) -> Result<QuoteAndVerify, Nlx402Error> {
        let quote = self.get_quote(total_price).await?;
        let verify = self.verify_quote(&quote, &quote.nonce).await?;
        Ok(QuoteAndVerify { quote, verify })
    }
}
