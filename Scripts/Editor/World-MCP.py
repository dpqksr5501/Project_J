"""Small sequential Streamable HTTP MCP client for the engine's loopback server.

Uses the advertised initialize -> tools/list -> tools/call protocol. Does not
enable remote Python, bypass tool restrictions, or change client configuration.
"""
import argparse
import json
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError


def request(port, method, params=None, session=None, identifier=1, version="2025-11-25"):
    message = {"jsonrpc": "2.0", "method": method}
    if identifier is not None:
        message["id"] = identifier
    if params is not None:
        message["params"] = params
    headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
               "Mcp-Protocol-Version": version}
    if session:
        headers["Mcp-Session-Id"] = session
    req = Request(f"http://127.0.0.1:{port}/mcp", json.dumps(message).encode(), headers)
    try:
        with urlopen(req, timeout=180) as response:
            body = response.read().decode()
            new_session = response.headers.get("Mcp-Session-Id", session)
    except HTTPError as error:
        raise RuntimeError(f'MCP HTTP {error.code}: {error.read().decode()}') from error
    if body.startswith("event:") or body.startswith("data:"):
        messages = [json.loads(line[5:].strip()) for line in body.splitlines() if line.startswith("data:")]
        return next((item for item in messages if item.get("id") == identifier), messages), new_session
    return (json.loads(body) if body.strip() else {}), new_session


def invoke(port, method, params):
    initialized, session = request(port, "initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
        "clientInfo": {"name": "ProjectJ-WorldAuthoring", "version": "1"}})
    if "error" in initialized:
        raise RuntimeError(initialized)
    version=initialized['result']['protocolVersion']
    request(port, "notifications/initialized", session=session, identifier=None,version=version)
    result, _ = request(port, method, params, session, 2,version)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--method", default="tools/list")
    parser.add_argument("--params", default="{}")
    parser.add_argument("--params-file", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    params = json.loads(args.params_file.read_text(encoding="utf-8-sig") if args.params_file else args.params)
    result = invoke(args.port, args.method, params)
    encoded = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    print(json.dumps({"output":str(args.output),"error":result.get('error'),"tool_error":result.get('result',{}).get('isError',False)})) if args.quiet else print(encoded)
