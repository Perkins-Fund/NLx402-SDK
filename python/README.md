# NLx402 Python SDK

### Example flow
```python
import os

from nlx402 import Nlx402Client, Nlx402Error


def main() -> None:
    # Prefer environment variable, fall back to a placeholder
    api_key = os.environ.get("NLX402_API_KEY", "YOUR_API_KEY_HERE")

    client = Nlx402Client(
        api_key=api_key,
    )

    try:
        metadata = client.get_metadata()
        print("=== Metadata ===")
        print(metadata)
        print()

        auth_me = client.get_auth_me()
        print("=== Auth / Me ===")
        print(auth_me)
        print()

        print("=== Get + Verify Quote ===")
        result = client.get_and_verify_quote(total_price=0.5)
        quote = result["quote"]
        verify = result["verify"]

        print("Quote:")
        print(quote)
        print()
        print("Verify response:")
        print(verify)
        print()


        print("=== Simulated Paid Access ===")
        print(
            "In production: wait for on-chain payment, then call "
            "get_paid_access(tx=<tx_signature>, nonce=quote['nonce'])."
        )


    except Nlx402Error as e:
        print(f"[NLx402 API ERROR] {e}")
    except Exception as e:
        print(f"[UNEXPECTED ERROR] {e}")


if __name__ == "__main__":
    main()
```