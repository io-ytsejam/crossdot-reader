# Reading statistics export API

The statistics export is available only while **File Transfer** is open on the
reader. The device displays an eight-digit one-time code on the server screen.

## Pair a client

Send the displayed code in the `X-CrossPoint-OTP` header:

```sh
curl -X POST \
  -H 'X-CrossPoint-OTP: 12345678' \
  http://crosspoint.local/api/statistics/auth
```

The response contains a bearer token:

```json
{"token":"00112233445566778899aabbccddeeff","tokenType":"Bearer","expires":"server-stop"}
```

The code can be exchanged only once. The token expires when File Transfer is
closed or the device restarts.

## Fetch statistics

```sh
curl \
  -H 'Authorization: Bearer 00112233445566778899aabbccddeeff' \
  http://crosspoint.local/api/statistics
```

The versioned response contains daily totals and per-book aggregates:

```json
{
  "schemaVersion": 1,
  "days": [
    {
      "date": "2026-09-21",
      "totalSeconds": 1820,
      "goalMet": true,
      "books": [
        {
          "title": "Example Book",
          "author": "Example Author",
          "activeSeconds": 1820,
          "lastReadAt": 1789945200
        }
      ]
    }
  ],
  "clockAvailable": true,
  "today": "2026-09-21",
  "currentStreak": 4
}
```

Book and cover paths on the SD card are intentionally omitted.

## Security boundary

The local server uses HTTP rather than HTTPS. The one-time exchange prevents an
unauthenticated client from reading statistics, but it does not encrypt traffic
or protect against an attacker able to intercept the local network connection.
