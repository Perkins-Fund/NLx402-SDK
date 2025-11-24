"""
NLx402 Python SDK

Minimal client for:
  - GET  /api/auth/me
  - GET  /api/metadata
  - GET  /protected
  - POST /verify
  - GET  /protected

Defaults to base URL: https://pay.thrt.ai
"""

from __future__ import annotations

import json
from typing import Any, Dict, Optional, Union

import requests


class Nlx402Error(Exception):
    """
    NLx402 HTTP error wrapper.
    """

    def __init__(self, message: str, status: int, body: Any = None) -> None:
        super().__init__(message)
        self.message = message
        self.status = status
        self.body = body

    def __str__(self) -> str:
        return f"{self.message} (status={self.status}, body={self.body})"


class Nlx402Client:
    """
    Minimal NLx402 client using requests.Session under the hood.
    """

    def __init__(
        self,
        api_key: Optional[str] = None,
        base_url: str = "https://pay.thrt.ai",
        session: Optional[requests.Session] = None,
    ) -> None:
        """
        :param api_key: Optional API key for authenticated endpoints.
        :param base_url: Base URL of the facilitator (default: https://pay.thrt.ai).
        :param session: Optional requests.Session to reuse connections.
        """
        self.base_url = base_url.rstrip("/")
        self.api_key = api_key
        self.session = session or requests.Session()

    def set_api_key(self, api_key: str) -> None:
        """
        Update / set API key at runtime.
        """
        self.api_key = api_key

    def _request(
        self,
        path: str,
        *,
        method: str = "GET",
        require_api_key: bool = False,
        headers: Optional[Dict[str, str]] = None,
        **kwargs: Any,
    ) -> Any:
        """
        Internal request helper.

        :param path: Path like "/api/auth/me".
        :param method: HTTP method.
        :param require_api_key: Whether x-api-key header is required.
        :param headers: Additional headers.
        :param kwargs: Extra arguments for requests.Session.request().
        :return: Parsed JSON body or raw text.
        :raises Nlx402Error: if response status is not 2xx.
        """
        url = f"{self.base_url}{path}"

        final_headers: Dict[str, str] = dict(headers or {})
        if require_api_key:
            if not self.api_key:
                raise ValueError(
                    "NLx402: API key is required but not set. "
                    "Call set_api_key() or pass it to Nlx402Client(...)."
                )
            final_headers.setdefault("x-api-key", self.api_key)

        response = self.session.request(method, url, headers=final_headers, **kwargs)

        text = response.text
        try:
            body = response.json() if text else None
        except ValueError:
            body = text

        if not response.ok:
            raise Nlx402Error(
                f"NLx402 request failed with status {response.status_code}",
                response.status_code,
                body,
            )

        return body

    def get_auth_me(self) -> Dict[str, Any]:
        """
        GET /api/auth/me

        Introspect current API key.

        Returns:
            {
              "ok": bool,
              "created_at": number,
              "wallet_id": string,
              "selected_mint": string
            }
        """
        return self._request(
            "/api/auth/me",
            method="GET",
            require_api_key=True,
        )

    def get_metadata(self) -> Dict[str, Any]:
        """
        GET /api/metadata

        Public metadata about the facilitator.

        Returns:
            {
              "ok": bool,
              "metadata": {
                "network": string,
                "supported_chains": [string],
                "version": string
              },
              "supported_mints": [string]   # or supported_tokens depending on backend
            }
        """
        return self._request(
            "/api/metadata",
            method="GET",
            require_api_key=False,
        )

    def get_quote(self, total_price: Union[int, float, str, None] = None) -> Dict[str, Any]:
        """
        Step 1 — GET /protected

        Get a quote (amount, mint, nonce, expiry).

        Always sends X-Total-Price, defaulting to 0.5 if not provided.

        :param total_price: Value to send in X-Total-Price header (e.g. 0.002).
                            Defaults to 0.5 if None.
        :return: QuoteResponse dict:
            {
              "amount": string,
              "chain": string,
              "decimals": number,
              "expires_at": number,
              "mint": string,
              "network": string,
              "nonce": string,
              "recipient": string,
              "version": string
            }
        """
        headers: Dict[str, str] = {}

        total = 0.5 if total_price is None else total_price
        headers["x-total-price"] = str(total)

        return self._request(
            "/protected",
            method="GET",
            headers=headers,
            require_api_key=True,
        )

    def verify_quote(self, *, quote: Union[Dict[str, Any], str], nonce: str) -> Dict[str, Any]:
        """
        Step 2 — POST /verify

        Lock/verify a quote. The server re-derives the quote
        server-side and checks that nothing was tampered with.

        :param quote: Object from get_quote() or a JSON string.
        :param nonce: Same nonce from the quote.
        :return: VerifyResponse dict:
            { "ok": bool }
        """
        if not nonce:
            raise ValueError('verify_quote: "nonce" is required')

        if isinstance(quote, str):
            payment_data = quote
        else:
            # Compact JSON for consistency
            payment_data = json.dumps(quote, separators=(",", ":"))

        data = {
            "payment_data": payment_data,
            "nonce": nonce,
        }

        headers = {
            "content-type": "application/x-www-form-urlencoded",
        }

        return self._request(
            "/verify",
            method="POST",
            headers=headers,
            data=data,
            require_api_key=True,
        )

    def get_paid_access(self, *, tx: str, nonce: str) -> Dict[str, Any]:
        if not tx or not nonce:
            raise ValueError('get_paid_access: "tx" and "nonce" are required')

        x_payment = json.dumps({"tx": tx, "nonce": nonce})
        headers = {"x-payment": x_payment}

        return self._request(
            "/protected",
            method="GET",
            headers=headers,
            require_api_key=True,
        )

    def get_and_verify_quote(self, total_price: Union[int, float, str, None] = None) -> Dict[str, Any]:
        """
        Convenience flow: get a quote, immediately verify it.

        You still need to:
          - pass the quote to the client for payment on-chain
          - call get_paid_access(tx=..., nonce=...) after payment

        :param total_price: Optional price for X-Total-Price (defaults to 0.5).
        :return: {
            "quote": QuoteResponse,
            "verify": VerifyResponse
        }
        """
        quote = self.get_quote(total_price=total_price)
        nonce = quote.get("nonce")
        if not nonce:
            raise ValueError("get_and_verify_quote: quote did not contain a 'nonce' field")
        verify = self.verify_quote(quote=quote, nonce=nonce)
        return {"quote": quote, "verify": verify}
