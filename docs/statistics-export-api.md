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
  "schemaVersion": 2,
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
          "lastReadAt": 1789945200,
          "path": "/books/example.epub",
          "coverBmpPath": "/books/example.bmp"
        }
      ]
    }
  ],
  "clockAvailable": true,
  "today": "2026-09-21",
  "currentStreak": 4
}
```

`schemaVersion` is currently `2`. Version 2 adds the `path` and `coverBmpPath`
of each book so the document can be imported back into a device losslessly
(books stay tied to on-device files and merge by path). Version 1 omitted those
two fields; the import endpoint still accepts it and keys those books by
title+author instead.

## Import statistics

The import endpoint restores/preserves reading statistics on a device — most
usefully to move them from one reader to another (e.g. X3 → X4). It accepts the
same document the export produces (both schema v1 and v2) and **merges** into
the device's existing statistics: per-day, per-book totals accumulate, so
re-importing the same data is idempotent rather than destructive.

```sh
curl -X POST \
  -H 'Authorization: Bearer 001122...eeff' \
  --data-binary @statistics.json \
  http://crosspoint.local/api/statistics/import
```

On success the device responds:

```json
{"ok":true,"booksImported":42}
```

`booksImported` counts the books actually merged. A request without a valid
bearer token is rejected with `401`; a malformed document (bad date, negative
time, or unparseable JSON) returns `400`. The body is streamed and merged as it
arrives, so importing a large document does not need to be buffered in RAM — but
closing the connection mid-way can still leave the books already received
written, exactly like an interrupted file upload.

The same pairing flow as the export applies: open File Transfer, swap the
one-time code for a token, and present that token as `Authorization: Bearer`.
The pairing covers both endpoints.

## Security boundary

The local server uses HTTP rather than HTTPS. The one-time exchange prevents an
unauthenticated client from reading statistics, but it does not encrypt traffic
or protect against an attacker able to intercept the local network connection.
