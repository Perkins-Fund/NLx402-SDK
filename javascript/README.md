# NLx402 NodeJS SDK

### Example flow

1. Get the quote by making a request to your paywall
```javascript
const quoteRes = await fetch('/paywall/quote', {
  method: 'POST',
  headers: { 'content-type': 'application/json' },
  body: JSON.stringify({ ... }),
}).then(r => r.json());

const quote = quoteRes.quote;
```

2. Verify the quote from the client before proceeding with purchase
```javascript
await fetch('/paywall/verify', {
  method: 'POST',
  headers: { 'content-type': 'application/json' },
  body: JSON.stringify({
    quote,
    nonce: quote.nonce,
  }),
});
```

3. Pay the onchain amount through whatever means you need to
4. Unlock the data
```javascript
const unlockRes = await fetch('/paywall/unlock', {
  method: 'POST',
  headers: { 'content-type': 'application/json' },
  body: JSON.stringify({
    tx: txSignature,
    nonce: quote.nonce,
  }),
}).then(r => r.json());

console.log(unlockRes.data); 
```


### Minimalist server examples

```javascript
// server.js
require('dotenv').config();
const express = require('express');
const { Nlx402Client, Nlx402Error } = require('./nlx402'); // the SDK file from before

const app = express();
app.use(express.json());

const nlx = new Nlx402Client({
  apiKey: process.env.NLX402_API_KEY,
});

const verifiedNonces = new Set();

app.get('/paywall/me', async (req, res) => {
  try {
    const me = await nlx.getAuthMe();
    res.json(me);
  } catch (err) {
    if (err instanceof Nlx402Error) {
      return res.status(err.status).json({ ok: false, error: err.body || err.message });
    }
    res.status(500).json({ ok: false, error: err.message });
  }
});

app.post('/paywall/quote', async (req, res) => {
  try {
    const totalPrice = req.body.totalPrice;

    const quote = await nlx.getQuote({
      totalPrice, 
    });
    
    res.json({
      ok: true,
      quote,
    });
  } catch (err) {
    if (err instanceof Nlx402Error) {
      return res.status(err.status).json({ ok: false, error: err.body || err.message });
    }
    res.status(500).json({ ok: false, error: err.message });
  }
});


app.post('/paywall/verify', async (req, res) => {
  try {
    const { quote, nonce } = req.body;

    if (!quote || !nonce) {
      return res.status(400).json({ ok: false, error: 'quote and nonce are required' });
    }

    const result = await nlx.verifyQuote({ quote, nonce });

    if (result.ok) {
      verifiedNonces.add(nonce);
    }

    res.json(result);
  } catch (err) {
    if (err instanceof Nlx402Error) {
      return res.status(err.status).json({ ok: false, error: err.body || err.message });
    }
    res.status(500).json({ ok: false, error: err.message });
  }
});

app.post('/paywall/unlock', async (req, res) => {
  try {
    const { tx, nonce } = req.body;

    if (!tx || !nonce) {
      return res.status(400).json({ ok: false, error: 'tx and nonce are required' });
    }
    
    if (!verifiedNonces.has(nonce)) {
      return res.status(400).json({
        ok: false,
        error: 'Nonce has not been verified yet. Call /paywall/verify first.',
      });
    }

    const paid = await nlx.getPaidAccess({ tx, nonce });

    if (!paid.ok || !paid.x402 || paid.x402.status !== 'paid') {
      return res.status(402).json({
        ok: false,
        error: 'Payment not confirmed for this nonce',
        x402: paid.x402 || null,
      });
    }

    const secretData = {
      message: 'Here is the protected resource.',
      extra: 'Only visible after a valid NLx402 payment.',
      x402: paid.x402,
    };

    res.json({
      ok: true,
      data: secretData,
    });
  } catch (err) {
    if (err instanceof Nlx402Error) {
      return res.status(err.status).json({ ok: false, error: err.body || err.message });
    }
    res.status(500).json({ ok: false, error: err.message });
  }
});

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
  console.log(`Paywall demo server listening on http://localhost:${PORT}`);
});
```