import os
import requests
import base64
import urllib.parse
import webbrowser
from http.server import HTTPServer, BaseHTTPRequestHandler
from dotenv import load_dotenv

load_dotenv()

CLIENT_ID     = os.environ["SPOTIFY_CLIENT_ID"]
CLIENT_SECRET = os.environ["SPOTIFY_CLIENT_SECRET"]
REDIRECT_URI  = "http://127.0.0.1:8888/callback"
SCOPE         = "user-read-currently-playing user-read-playback-state"

auth_url = (
    "https://accounts.spotify.com/authorize?"
    + urllib.parse.urlencode({
        "client_id": CLIENT_ID,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "scope": SCOPE,
    })
)

print("Opening browser for Spotify login...")
webbrowser.open(auth_url)

class Handler(BaseHTTPRequestHandler):

    def do_GET(self):
        if "/callback" not in self.path:
            self.send_response(200)
            self.end_headers()
            return

        print("\nFull callback URL:")
        print(self.path)

        params = urllib.parse.parse_qs(
            urllib.parse.urlparse(self.path).query
        )
        print("\nParams received:", params)

        if "code" not in params:
            print("\nERROR: No code received.")
            print(params.get("error", "unknown error"))
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"Error - check terminal")
            return

        code = params["code"][0]
        creds = base64.b64encode(
            f"{CLIENT_ID}:{CLIENT_SECRET}".encode()
        ).decode()

        r = requests.post(
            "https://accounts.spotify.com/api/token",
            data={
                "grant_type": "authorization_code",
                "code": code,
                "redirect_uri": REDIRECT_URI,
            },
            headers={"Authorization": f"Basic {creds}"}
        )

        tokens = r.json()
        print("\n=== YOUR REFRESH TOKEN ===")
        print(tokens.get("refresh_token", "ERROR: " + str(tokens)))

        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"Got it! Check your terminal.")

    def log_message(self, format, *args):
        pass

server = HTTPServer(("127.0.0.1", 8888), Handler)
server.handle_request()
server.handle_request()