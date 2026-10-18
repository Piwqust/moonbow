"""Local mock of the LRCLIB, OpenSubtitles and TMDB APIs for testing Lumen's online features without keys.

Binds 127.0.0.1 only. Answers have the shape of the real services (recorded from their docs).
Point a --automation Lumen at it with the hidden settings:
  privacy.lrclibServer        = http://127.0.0.1:<port>
  privacy.opensubtitlesServer = http://127.0.0.1:<port>/api/v1
  privacy.tmdbServer          = http://127.0.0.1:<port>/3
  privacy.tmdbImageServer     = http://127.0.0.1:<port>/t/p

Usage: python qa/round7/online-mock.py <port> <log file> <image folder>
The image folder holds poster.jpg, backdrop.jpg and still.jpg (any JPEGs).

Error switches, by key or by query text:
  key "bad"              -> 401 (both services)
  query containing "ratelimit" -> 429 with Retry-After: 3
  query containing "slow"      -> answers after 25 s (Lumen times out at 15)
  OpenSubtitles downloads: 3 per run, then 406 with the quota message.
Every request is appended to the log as one JSON line (method, path, a few headers).
"""
import json
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1])
LOG = sys.argv[2]
IMAGES = sys.argv[3]
lock = threading.Lock()
state = {"downloads": 3}

SRT = "1\r\n00:00:01,000 --> 00:00:04,000\r\nMock subtitle from the test server\r\n\r\n2\r\n00:00:05,000 --> 00:00:08,000\r\nВторая строка\r\n"


def os_item(i, lang, release, file_id, hash_match=False, hi=False, foreign=False, downloads=1000, rating=8.0):
    return {
        "id": str(i), "type": "subtitle",
        "attributes": {
            "subtitle_id": str(i), "language": lang, "download_count": downloads, "new_download_count": 10,
            "hearing_impaired": hi, "hd": True, "fps": 23.976, "votes": 4, "ratings": rating,
            "from_trusted": True, "foreign_parts_only": foreign, "upload_date": "2020-01-01T00:00:00Z",
            "ai_translated": False, "machine_translated": False, "release": release, "comments": "",
            "legacy_subtitle_id": i, "uploader": {"uploader_id": 1, "name": "mock", "rank": "admin"},
            "feature_details": {"feature_id": 1, "feature_type": "Movie", "year": 2010, "title": "Inception",
                                "movie_name": "2010 - Inception", "imdb_id": 1375666, "tmdb_id": 27205},
            "url": "https://www.opensubtitles.com/en/subtitles/mock", "related_links": [],
            "files": [{"file_id": file_id, "cd_number": 1, "file_name": release + ".srt"}],
            "moviehash_match": hash_match,
        },
    }


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def record(self, body=b""):
        entry = {"t": round(time.time(), 3), "method": self.command, "path": self.path,
                 "ua": self.headers.get("User-Agent"), "apiKey": bool(self.headers.get("Api-Key")),
                 "auth": (self.headers.get("Authorization") or "")[:12], "body": body.decode("utf-8", "replace")[:200]}
        with lock:
            with open(LOG, "a", encoding="utf-8") as f:
                f.write(json.dumps(entry, ensure_ascii=False) + "\n")

    def send(self, status, obj=None, raw=None, ctype="application/json", headers=None):
        data = raw if raw is not None else json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self.record()
        self.route()

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        self.record(body)
        self.route(body)

    def route(self, body=b""):
        url = urllib.parse.urlsplit(self.path)
        q = urllib.parse.parse_qs(url.query)
        text = " ".join(v[0] for v in q.values()).lower()
        path = url.path
        if "slow" in text:
            time.sleep(25)
        if "ratelimit" in text:
            return self.send(429, {"message": "API rate limit exceeded"}, headers={"Retry-After": "3"})
        if path.startswith("/api/get") or path.startswith("/api/search"):
            return self.lrclib(path, q)
        if path.startswith("/api/v1/"):
            return self.opensubtitles(path[len("/api/v1"):], q, body)
        if path.startswith("/3/"):
            return self.tmdb(path[2:], q)
        if path.startswith("/t/p/"):
            name = path.rsplit("/", 1)[-1]
            kind = "poster" if path.startswith("/t/p/w342") else "still" if path.startswith("/t/p/w300") else "backdrop"
            with open(f"{IMAGES}\\{kind}.jpg", "rb") as f:
                return self.send(200, raw=f.read(), ctype="image/jpeg")
        if path.startswith("/dl/"):
            return self.send(200, raw=SRT.encode("utf-8"), ctype="application/x-subrip")
        self.send(404, {"message": "not found"})

    def opensubtitles(self, path, q, body):
        if self.headers.get("Api-Key") in (None, "", "bad"):
            return self.send(401, {"message": "You cannot consume this service", "status": 401})
        if path == "/subtitles":
            items = []
            if "nothing" not in (q.get("query", [""])[0]):
                langs = q.get("languages", ["en,ru"])[0].split(",")
                hashed = "moviehash" in q
                i = 1
                for lang in langs:
                    items.append(os_item(i, lang, f"Inception.2010.1080p.BluRay.{lang}", 1000 + i, hash_match=hashed, downloads=12345))
                    i += 1
                    items.append(os_item(i, lang, f"Inception.2010.720p.WEB-DL.HI.{lang}", 1000 + i, hi=True, downloads=345, rating=6.5))
                    i += 1
                items.append(os_item(i, langs[0], "Inception.2010.Forced.Foreign.Parts", 1000 + i, foreign=True, downloads=7, rating=0))
            return self.send(200, {"total_pages": 1, "total_count": len(items), "per_page": 60, "page": 1, "data": items})
        if path == "/login":
            creds = json.loads(body or b"{}")
            if creds.get("password") != "good":
                return self.send(401, {"message": "Error, invalid username/password", "status": 401})
            return self.send(200, {"user": {"allowed_translations": 1, "allowed_downloads": 20, "level": "Sub leecher",
                                            "user_id": 1, "ext_installed": False, "vip": False},
                                   "base_url": "api.opensubtitles.com", "token": "mock-jwt-token", "status": 200})
        if path == "/infos/user":
            with lock:
                left = state["downloads"]
            return self.send(200, {"data": {"allowed_downloads": 20, "level": "Sub leecher", "user_id": 1,
                                            "ext_installed": False, "vip": False, "downloads_count": 20 - left,
                                            "remaining_downloads": left}})
        if path == "/download":
            fid = json.loads(body or b"{}").get("file_id")
            with lock:
                if state["downloads"] <= 0:
                    return self.send(406, {"requests": 3, "remaining": 0,
                                           "message": "You have downloaded your allowed 3 subtitles for 24h.",
                                           "reset_time": "23 hours and 59 minutes", "reset_time_utc": "2026-10-19T12:00:00.000Z"})
                state["downloads"] -= 1
                left = state["downloads"]
            return self.send(200, {"link": f"http://127.0.0.1:{PORT}/dl/{fid}/mock.srt", "file_name": f"mock-{fid}.srt",
                                   "requests": 3 - left, "remaining": left, "message": "ok",
                                   "reset_time": "23 hours", "reset_time_utc": "2026-10-19T12:00:00.000Z"})
        self.send(404, {"message": "not found"})

    def lrclib(self, path, q):
        # Lumen's own test lines (not a real song), synced.
        record = {"id": 1, "trackName": q.get("track_name", [""])[0], "artistName": q.get("artist_name", [""])[0],
                  "albumName": q.get("album_name", [""])[0], "duration": float(q.get("duration", ["0"])[0] or 0),
                  "instrumental": False,
                  "plainLyrics": "Mock line one\nMock line two\nMock line three",
                  "syncedLyrics": "[00:00.50] Mock line one, from the test server\n[00:02.00] Строка два\n"
                                  "[00:04.00] 第三行\n[00:06.00] Line four is a little longer than the others\n"
                                  "[00:08.00] Mock line five"}
        if "nothing" in record["trackName"].lower():
            return self.send(404, {"code": 404, "name": "TrackNotFound", "message": "Failed to find specified track"})
        if path.startswith("/api/search"):
            return self.send(200, [record])
        return self.send(200, record)

    def tmdb(self, path, q):
        key = q.get("api_key", [""])[0]
        auth = self.headers.get("Authorization") or ""
        if key == "bad" or (not key and not auth.startswith("Bearer ")) or auth == "Bearer bad":
            return self.send(401, {"status_code": 7, "status_message": "Invalid API key: You must be granted a valid key.", "success": False})
        query = q.get("query", [""])[0].lower()
        if path == "/search/movie":
            results = []
            # "matrix" and "nothing" have their own answers; anything else is Inception, so the
            # shared test media (Chaptered Demo, Series S01E01...) gets a match too.
            if "matrix" not in query and "nothing" not in query:
                results = [
                    {"adult": False, "backdrop_path": "/s3TBrRGB1iav7gFOCNx3H31MoES.jpg", "genre_ids": [28, 878], "id": 27205,
                     "original_language": "en", "original_title": "Inception",
                     "overview": "Cobb, a skilled thief who commits corporate espionage by infiltrating the subconscious of his targets, is offered a chance to regain his old life.",
                     "popularity": 100.0, "poster_path": "/ljsZTbVsrQSqZgWeep2B1QiDKuh.jpg", "release_date": "2010-07-15",
                     "title": "Inception", "video": False, "vote_average": 8.369, "vote_count": 35000},
                    {"adult": False, "backdrop_path": None, "id": 64956, "original_title": "Inception: The Cobol Job",
                     "overview": "A prequel comic.", "poster_path": "/sNxqwtyHMNQwKWoFYDqcYTui5Ok.jpg",
                     "release_date": "2010-12-07", "title": "Inception: The Cobol Job", "vote_average": 7.2, "vote_count": 300},
                ]
            elif "matrix" in query:
                results = [{"id": 603, "title": "The Matrix", "release_date": "1999-03-31", "overview": "Neo.",
                            "poster_path": "/f89U3ADr1oiB1s9GkdPOEpXUk5H.jpg", "backdrop_path": "/fNG7i7RqMErkcqhohV2a6cV1Ehy.jpg",
                            "vote_average": 8.2}]
            return self.send(200, {"page": 1, "results": results, "total_pages": 1, "total_results": len(results)})
        if path == "/search/tv":
            results = []
            if "nothing" not in query:
                results = [{"backdrop_path": "/mLyW3UTgi2lsMdtueYODcfAB9Ku.jpg", "first_air_date": "2005-03-24", "id": 2316,
                            "name": "The Office", "original_name": "The Office", "overview": "The everyday lives of office employees.",
                            "poster_path": "/7DJKHzAi83BmQrWLrYYOqcoKfhR.jpg", "vote_average": 8.6, "vote_count": 4000}]
            return self.send(200, {"page": 1, "results": results, "total_pages": 1, "total_results": len(results)})
        if path.startswith("/tv/2316/season/"):
            parts = path.split("/")  # ['', 'tv', '2316', 'season', S, 'episode', E]
            return self.send(200, {"air_date": "2005-10-18", "episode_number": int(parts[6]), "id": 1,
                                   "name": "Halloween", "overview": "Michael has to fire someone by the end of the day.",
                                   "season_number": int(parts[4]), "still_path": "/wjpRo3DlBJwKUXvVxgM2HmBfn6V.jpg",
                                   "vote_average": 7.9})
        self.send(404, {"status_code": 34, "status_message": "The resource you requested could not be found."})


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
