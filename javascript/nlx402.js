/**
 * NLx402 Node.js SDK
 *
 * Minimal client for:
 *  - GET  /api/auth/me
 *  - GET  /api/metadata
 *  - GET  /protected
 *  - POST /verify
 *  - GET  /protected
 *
 * Defaults to base URL: https://pay.thrt.ai
 */

/**
 * @typedef {Object} Nlx402ClientOptions
 * @property {string} [apiKey]
 * @property {string} [baseUrl]
 * @property {typeof fetch} [fetch]
 */

/**
 * @typedef {Object} AuthMeResponse
 * @property {boolean} ok
 * @property {number} created_at
 * @property {string} wallet_id
 * @property {string} selected_mint
 */

/**
 * @typedef {Object} MetadataResponse
 * @property {boolean} ok
 * @property {{network:string, supported_chains:string[], version:string}} metadata
 * @property {string[]} supported_mints
 */

/**
 * @typedef {Object} QuoteResponse
 * @property {string} amount
 * @property {string} chain
 * @property {number} decimals
 * @property {number} expires_at
 * @property {string} mint
 * @property {string} network
 * @property {string} nonce
 * @property {string} recipient
 * @property {string} version
 */

/**
 * @typedef {Object} VerifyResponse
 * @property {boolean} ok
 */

/**
 * @typedef {Object} PaidAccessResponse
 * @property {boolean} ok
 * @property {{
 *   amount: string,
 *   decimals: number,
 *   mint: string,
 *   nonce: string,
 *   status: string,
 *   tx: string,
 *   version: string
 * }} x402
 */

class Nlx402Error extends Error {
  /**
   * @param {string} message
   * @param {number} status
   * @param {any} [body]
   */
  constructor(message, status, body) {
    super(message);
    this.name = 'Nlx402Error';
    this.status = status;
    this.body = body;
  }
}

class Nlx402Client {
  /**
   * @param {Nlx402ClientOptions} [options]
   */
  constructor(options = {}) {
    this.baseUrl = (options.baseUrl || 'https://pay.thrt.ai').replace(/\/+$/, '');
    this.apiKey = options.apiKey || null;
    this._fetch = options.fetch || globalThis.fetch;

    if (!this._fetch) {
      throw new Error(
        'No fetch implementation found. ' +
          'Provide options.fetch or use Node.js 18+ where global fetch is available.'
      );
    }
  }

  /**
   * Update / set API key at runtime.
   * @param {string} apiKey
   */
  setApiKey(apiKey) {
    this.apiKey = apiKey;
  }

  /**
   * Internal request helper.
   * @param {string} path
   * @param {RequestInit & {requireApiKey?: boolean}} [options]
   * @returns {Promise<any>}
   * @private
   */
  async _request(path, options = {}) {
    const url = `${this.baseUrl}${path}`;
    const { requireApiKey, headers, ...rest } = options;

    /** @type {Record<string,string>} */
    const finalHeaders = {
      ...(headers || {}),
    };

    if (requireApiKey) {
      if (!this.apiKey) {
        throw new Error('NLx402: API key is required but not set. Call setApiKey() or pass in constructor.');
      }
      finalHeaders['x-api-key'] = this.apiKey;
    }

    const res = await this._fetch(url, {
      ...rest,
      headers: finalHeaders,
    });

    let body;
    const text = await res.text();
    try {
      body = text ? JSON.parse(text) : null;
    } catch {
      body = text;
    }

    if (!res.ok) {
      throw new Nlx402Error(
        `NLx402 request failed with status ${res.status}`,
        res.status,
        body
      );
    }

    return body;
  }

  /**
   * GET /api/auth/me
   *
   * Introspect current API key.
   * @returns {Promise<AuthMeResponse>}
   */
  async getAuthMe() {
    return /** @type {Promise<AuthMeResponse>} */ (
      this._request('/api/auth/me', {
        method: 'GET',
        requireApiKey: true,
      })
    );
  }

  /**
   * GET /api/metadata
   *
   * Public metadata about the facilitator.
   * @returns {Promise<MetadataResponse>}
   */
  async getMetadata() {
    return /** @type {Promise<MetadataResponse>} */ (
      this._request('/api/metadata', {
        method: 'GET',
      })
    );
  }

  /**
   * Step 1 — GET /protected
   *
   * Get a quote (amount, mint, nonce, expiry).
   *
   * Always sends X-Total-Price, defaulting to 0.5 if not provided.
   *
   * @param {{ totalPrice?: number|string } } [options]
   *   totalPrice → sends X-Total-Price header, e.g. 0.002
   *
   * @returns {Promise<QuoteResponse>}
   */
  async getQuote(options = {}) {
    /** @type {Record<string,string>} */
    const headers = {};

    // Always send X-Total-Price, default 0.5
    const total = options.totalPrice ?? 0.5;
    headers['x-total-price'] = String(total);

    return /** @type {Promise<QuoteResponse>} */ (
      this._request('/protected', {
        method: 'GET',
        headers,
        requireApiKey: true,
      })
    );
  }

  /**
   * Step 2 — POST /verify
   *
   * Lock/verify a quote. The server re-derives the quote
   * server-side and checks that nothing was tampered with.
   *
   * @param {{ quote: QuoteResponse | string, nonce: string }} params
   *   quote → object from getQuote() or a JSON string
   *   nonce → same nonce from the quote
   *
   * @returns {Promise<VerifyResponse>}
   */
  async verifyQuote(params) {
    const { quote, nonce } = params;
    if (!nonce) {
      throw new Error('verifyQuote: "nonce" is required');
    }

    const paymentData =
      typeof quote === 'string' ? quote : JSON.stringify(quote);

    const body = new URLSearchParams();
    body.set('payment_data', paymentData);
    body.set('nonce', nonce);

    return /** @type {Promise<VerifyResponse>} */ (
      this._request('/verify', {
        method: 'POST',
        body,
        headers: {
          'content-type': 'application/x-www-form-urlencoded',
        },
        requireApiKey: true,
      })
    );
  }

  /**
   * Step 3 — GET /protected with X-Payment
   *
   * Access the protected resource after the user has paid on-chain.
   *
   * @param {{ tx: string, nonce: string }} params
   *   tx    → Solana transaction signature
   *   nonce → quote nonce used when verifying
   *
   * @returns {Promise<PaidAccessResponse>}
   */
  async getPaidAccess(params) {
    const { tx, nonce } = params;
    if (!tx || !nonce) {
      throw new Error('getPaidAccess: "tx" and "nonce" are required');
    }

    const xPayment = JSON.stringify({ tx, nonce });

    return /** @type {Promise<PaidAccessResponse>} */ (
      this._request('/protected', {
        method: 'GET',
        headers: {
          'x-payment': xPayment,
        },
        requireApiKey: true,
      })
    );
  }

  /**
   * Convenience flow: get a quote, immediately verify it.
   *
   * You still need to:
   *  - pass the quote to the client for payment on-chain
   *  - call getPaidAccess({tx, nonce}) after payment
   *
   * @param {{ totalPrice?: number|string }} [options]
   * @returns {Promise<{quote: QuoteResponse, verify: VerifyResponse}>}
   */
  async getAndVerifyQuote(options = {}) {
    const quote = await this.getQuote(options); // will default to 0.5 if not provided
    const verify = await this.verifyQuote({
      quote,
      nonce: quote.nonce,
    });
    return { quote, verify };
  }
}

module.exports = {
  Nlx402Client,
  Nlx402Error,
};
