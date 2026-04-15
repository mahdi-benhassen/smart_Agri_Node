#!/usr/bin/env python3
"""
SAGRI OTA Server — ota_server.py
A simple HTTP server to serve firmware images for the Gateway's OTA update.
It provides a version endpoint that the gateway checks, and hosts the .bin file.

Usage:
  # First, put your new gateway firmware bin in the tools/ directory
  python ota_server.py --port 8000 --bin sagri_gateway.bin --version "1.5.0"
"""

import http.server
import socketserver
import json
import argparse
import os

class OTAHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def do_GET(self):
        # Version check endpoint
        if self.path == '/api/v1/version?target=gw':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            
            response = {
                "target": "gw",
                "version": self.server.fw_version,
                "url": f"http://{self.server.server_address[0]}:{self.server.server_port}/{self.server.bin_file}"
            }
            self.wfile.write(json.dumps(response).encode())
            return

        # Explicitly restrict to serving the bin file for security (simple implementation)
        if self.path == f"/{self.server.bin_file}":
            super().do_GET()
            return
            
        self.send_error(404, "File not found.")

def main():
    parser = argparse.ArgumentParser(description="SAGRI Simple OTA HTTP Server")
    parser.add_argument("--port", type=int, default=8000, help="Port to listen on")
    parser.add_argument("--bin", required=True, help="Firmware binary file to serve")
    parser.add_argument("--version", required=True, help="Semantic version of the binary (e.g. 1.5.0)")
    
    args = parser.parse_args()

    if not os.path.exists(args.bin):
        print(f"[!] Error: Firmware file '{args.bin}' not found.")
        return

    # Create server and pass args to it
    Handler = OTAHandler
    
    with socketserver.TCPServer(("", args.port), Handler) as httpd:
        httpd.fw_version = args.version
        httpd.bin_file = os.path.basename(args.bin)
        
        # Change dir so the base HTTP handler can find the file
        os.chdir(os.path.dirname(os.path.abspath(args.bin)) or ".")
        
        print(f"[*] SAGRI OTA Server running on port {args.port}")
        print(f"[*] Serving '{httpd.bin_file}' as version {httpd.fw_version}")
        print(f"[*] Endpoint: http://<your-ip>:{args.port}/api/v1/version?target=gw")
        print(f"[*] Press Ctrl+C to stop")
        
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[*] Stopping server...")

if __name__ == "__main__":
    main()
