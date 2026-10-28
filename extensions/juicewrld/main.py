
#!/usr/bin/env python3
"""Lumen source extension for the Juice WRLD API.

Searches the community API catalogue and resolves audio/art through its file
endpoints. Recording-session entries are deliberately excluded.
"""
import json
import sys
import threading
import urllib.error
import urllib.parse
import urllib.request

API = "https://juicewrldapi.com/juicewrld"
SITE = "https://juicewrldapi.com"

# Only include these catalogue categories.
CATEGORIES = ("released", "unreleased", "unsurfaced")

OUT_LOCK = threading.Lock()
TIMEOUT = 15
USER_AGENT = "Lumen-JuiceWRLD-Source/1.0"


def emit(obj):
    """Write one JSON-lines response to stdout."""
    with OUT_LOCK:
        sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
        sys.stdout.flush()


def api_get(path, params=None):
    """Request JSON from the Juice WRLD API."""
    url = API.rstrip("/") + "/" + path.lstrip("/")

    if params:
        url += "?" + urllib.parse.urlencode(params)

    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": USER_AGENT,
            "Accept": "application/json",
        },
    )

    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as response:
            return json.loads(
                response.read().decode("utf-8")
            )

    except urllib.error.HTTPError as exc:
        raise RuntimeError(
            "Juice WRLD API returned HTTP %s for %s"
            % (exc.code, path)
        )

    except urllib.error.URLError as exc:
        raise RuntimeError(
            "Couldn't reach the Juice WRLD API: %s"
            % exc.reason
        )

    except json.JSONDecodeError:
        raise RuntimeError(
            "The Juice WRLD API returned invalid JSON."
        )


def full_url(value):
    """Convert a relative API URL into an absolute URL."""
    if not value:
        return ""

    if value.startswith(("https://", "http://", "data:")):
        return value

    return urllib.parse.urljoin(
        SITE + "/",
        value.lstrip("/")
    )


def playable_url(path):
    """Build the API file-download URL for a catalogue path."""
    return API + "/files/download/?" + urllib.parse.urlencode({
        "path": path
    })


def artwork_url(song):
    """Resolve artwork from the API."""
    path = song.get("path")

    if path:
        return API + "/files/cover-art/?" + urllib.parse.urlencode({
            "path": path,
            "small": "true",
        })

    return full_url(song.get("image_url") or "")


def category_of(song):
    """Normalize category names and reject recording sessions."""
    category = str(song.get("category") or "").strip().lower()

    if category == "recording_session":
        return ""

    return category


def is_in_scope(song):
    """Return True only for the categories we want."""
    return category_of(song) in CATEGORIES


def duration_ms(song):
    """Convert duration metadata to milliseconds where possible."""
    value = song.get("length") or song.get("duration") or ""

    if isinstance(value, (int, float)):
        return int(value * 1000 if value < 10000 else value)

    try:
        bits = [int(part) for part in str(value).split(":")]
        seconds = 0

        for bit in bits:
            seconds = seconds * 60 + bit

        return seconds * 1000

    except (ValueError, TypeError):
        return 0


def to_track(song):
    """Map an API song object to Lumen's track representation."""
    title = (
        song.get("name")
        or song.get("original_key")
        or "Untitled"
    )

    path = song.get("path") or ""
    category = category_of(song)

    era = song.get("era") or {}

    if isinstance(era, dict):
        album = song.get("album") or era.get("name") or "Juice WRLD"
        era_name = era.get("name", "")
    else:
        album = song.get("album") or "Juice WRLD"
        era_name = str(era or "")

    return {
        "id": str(song.get("id") or path or title),
        "title": title,
        "artist": "Juice WRLD",
        "album": album,
        "durationMs": duration_ms(song),
        "cover": artwork_url(song),
        "url": playable_url(path) if path else "",
        "source": "Juice WRLD API",
        "category": category,
        "era": era_name,
    }


def search(p, partial=None):
    """Search all supported catalogue categories."""
    query = str(p.get("query") or "").strip()

    limit = min(
        50,
        max(1, int(p.get("limit", 20)))
    )

    gathered = []
    errors = []

    per_category = max(5, min(50, limit))

    for category in CATEGORIES:
        params = {
            "page": 1,
            "page_size": per_category,
            "category": category,
        }

        if query:
            params["search"] = query

        try:
            payload = api_get("/songs/", params)

            if isinstance(payload, list):
                rows = payload
            else:
                rows = payload.get("results", [])

            for song in rows or []:
                if not isinstance(song, dict):
                    continue

                if not is_in_scope(song):
                    continue

                track = to_track(song)

                # Skip entries without a known playable file path.
                if not track["url"]:
                    continue

                gathered.append(track)

        except Exception as exc:
            errors.append(str(exc))

    # Deduplicate entries by their API ID.
    unique = {}

    for row in gathered:
        unique.setdefault(row["id"], row)

    rows = list(unique.values())

    # Put exact and partial title matches first.
    if query:
        q = query.casefold()

        rows.sort(
            key=lambda row: (
                0 if row["title"].casefold() == q
                else 1 if q in row["title"].casefold()
                else 2 if q in row["album"].casefold()
                else 3,
                row["title"].casefold(),
            )
        )
    else:
        rows.sort(
            key=lambda row: row["title"].casefold()
        )

    rows = rows[:limit]

    if partial and rows:
        partial(rows)

    if not rows and errors:
        raise RuntimeError(errors[0])

    return rows


def resolve(p):
    """Resolve a selected track to its current playback URL."""
    track_id = str(p.get("id") or "").strip()

    if not track_id:
        raise RuntimeError(
            "No Juice WRLD API track ID was supplied."
        )

    if not track_id.isdigit():
        raise RuntimeError(
            "This Juice WRLD API track ID is invalid."
        )

    song = api_get(
        "/songs/%s/" % urllib.parse.quote(track_id, safe="")
    )

    if not isinstance(song, dict) or not is_in_scope(song):
        raise RuntimeError(
            "This item is a recording session or is not available "
            "in this source."
        )

    path = song.get("path")

    if not path:
        raise RuntimeError(
            "The API has no playable file attached to this song."
        )

    return {
        "url": playable_url(path),
        "headers": {
            "User-Agent": USER_AGENT,
        },
        "format": (
            "mp3"
            if str(path).lower().endswith(".mp3")
            else "audio"
        ),
        "bitrateKbps": int(song.get("bitrate_kbps") or 0),
        "lossless": False,
    }


def recommend(p):
    """Return a simple recommendation list using era and album metadata."""
    limit = min(
        30,
        max(1, int(p.get("limit", 20)))
    )

    seed_title = str(p.get("title") or "").strip().casefold()
    seed_era = str(p.get("era") or "").strip()
    seed_album = str(p.get("album") or "").strip()

    rows = search({
        "query": "",
        "limit": 50,
    })

    def score(row):
        points = 0

        if (
            seed_era
            and row.get("era", "").casefold() == seed_era.casefold()
        ):
            points += 3

        if (
            seed_album
            and row.get("album", "").casefold() == seed_album.casefold()
        ):
            points += 2

        if seed_title and row.get("title", "").casefold() == seed_title:
            points -= 10

        return points

    rows.sort(
        key=lambda row: (
            -score(row),
            row.get("title", "").casefold(),
        )
    )

    return [
        row
        for row in rows
        if row.get("title", "").casefold() != seed_title
    ][:limit]


METHODS = {
    "source.search": search,
    "source.resolve": resolve,
    "source.recommend": recommend,
}


def handle(req):
    """Handle a single request from Lumen."""
    try:
        method = req["method"]
        params = req.get("params") or {}

        if method == "source.search":
            result = search(
                params,
                lambda rows: emit({
                    "id": req.get("id"),
                    "partial": True,
                    "result": rows,
                }),
            )

        elif method in METHODS:
            result = METHODS[method](params)

        else:
            raise RuntimeError(
                "Unknown method: %s" % method
            )

        emit({
            "id": req.get("id"),
            "result": result,
        })

    except Exception as exc:
        emit({
            "id": req.get("id"),
            "error": {
                "message": str(exc),
            },
        })


# JSON-lines protocol: one request per line, one response per request.
for line in sys.stdin:
    if not line.strip():
        continue

    try:
        request = json.loads(line)

    except Exception:
        continue

    threading.Thread(
        target=handle,
        args=(request,),
        daemon=True,
    ).start()
