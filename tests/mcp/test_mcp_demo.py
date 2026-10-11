#!/usr/bin/env python3
"""End-to-end test of the Tefra MCP server (spec M3, ADR-015). Standard library only.

Starts tefra_mcp_demo and talks Streamable HTTP to it: transport rules (405, 403, 415, 400, 404), the handshake,
the world tools (create, set, get, find, clone, reparent, scripts, delete, save and load), tool errors, and then the
tefra_mcp stdio bridge against the same server.

usage: test_mcp_demo.py <tefra_mcp_demo> <tefra_mcp>
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.request

PROTOCOL = "2025-06-18"
WORLD_TOOLS = {
    "get_tree", "get_instance", "list_classes", "create_instance", "set_properties", "delete_instance",
    "reparent", "clone", "find", "get_script", "set_script", "save_place", "load_place", "echo",
}
# HTTP_PROXY is often set in CI and cloud shells; loopback traffic must never go through it.
OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


class Failure(Exception):
    pass


def check(cond, what):
    if not cond:
        raise Failure(what)
    print("ok  - " + what)


def header(headers, name):
    for key, value in headers.items():
        if key.lower() == name.lower():
            return value
    return None


class Http:
    def __init__(self, port):
        self.url = "http://127.0.0.1:%d/mcp" % port
        self.session = None
        self.next_id = 1

    def post(self, body=None, headers=None, raw=None):
        data = raw if raw is not None else json.dumps(body).encode("utf-8")
        req = urllib.request.Request(self.url, data=data, method="POST")
        req.add_header("Content-Type", "application/json")
        req.add_header("Accept", "application/json, text/event-stream")
        if self.session:
            req.add_header("Mcp-Session-Id", self.session)
        for key, value in (headers or {}).items():
            req.add_header(key, value)
        try:
            with OPENER.open(req, timeout=30) as resp:
                return resp.status, dict(resp.headers), resp.read().decode("utf-8")
        except urllib.error.HTTPError as err:
            return err.code, dict(err.headers), err.read().decode("utf-8")

    def rpc(self, method, params=None):
        msg = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        self.next_id += 1
        if params is not None:
            msg["params"] = params
        status, _, text = self.post(msg)
        if status != 200:
            raise Failure("%s: HTTP %d: %s" % (method, status, text))
        return json.loads(text)

    def tool(self, name, arguments):
        """Calls a tool. Returns (is_error, payload): parsed JSON on success, the message text on error."""
        reply = self.rpc("tools/call", {"name": name, "arguments": arguments})
        if "error" in reply:
            raise Failure("%s: JSON-RPC error %s" % (name, reply["error"]))
        result = reply["result"]
        text = result["content"][0]["text"]
        if result.get("isError"):
            return True, text
        try:
            return False, json.loads(text)
        except ValueError:
            return False, text  # plain-text tools such as echo

    def ok(self, name, arguments):
        is_error, payload = self.tool(name, arguments)
        if is_error:
            raise Failure("%s failed: %s" % (name, payload))
        return payload


def value_of(instance, prop):
    return instance["properties"][prop]["value"]


def transport_checks(port):
    http = Http(port)
    try:
        OPENER.open(urllib.request.Request(http.url, method="GET"), timeout=10)
        raise Failure("GET /mcp should be refused")
    except urllib.error.HTTPError as err:
        check(err.code == 405, "GET /mcp is 405 (no SSE stream)")

    status, _, _ = http.post({"jsonrpc": "2.0", "id": 1, "method": "ping"},
                             headers={"Origin": "http://evil.example"})
    check(status == 403, "foreign Origin is refused with 403 (DNS rebinding defence)")

    status, _, _ = http.post(raw=b"{}", headers={"Content-Type": "text/plain"})
    check(status == 415, "non-JSON Content-Type is refused with 415")

    status, _, text = http.post(raw=b"{not json")
    check(status == 400 and json.loads(text)["error"]["code"] == -32700, "malformed JSON gets a 400 parse error")

    status, _, _ = http.post({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
    check(status == 400, "a request without Mcp-Session-Id is refused before initialize")

    status, _, _ = http.post({"jsonrpc": "2.0", "id": 3, "method": "tools/list"},
                             headers={"Mcp-Session-Id": "0123deadbeef"})
    check(status == 404, "an unknown Mcp-Session-Id gets 404 (client must initialize again)")


def handshake_checks(http):
    status, headers, text = http.post({
        "jsonrpc": "2.0", "id": 1, "method": "initialize",
        "params": {"protocolVersion": PROTOCOL, "capabilities": {},
                   "clientInfo": {"name": "test", "version": "0"}}})
    check(status == 200, "initialize answers 200")
    http.session = header(headers, "Mcp-Session-Id")
    check(bool(http.session), "initialize returns an Mcp-Session-Id header")
    result = json.loads(text)["result"]
    check(result["protocolVersion"] == PROTOCOL, "protocolVersion is 2025-06-18")
    check(result["serverInfo"]["name"] == "tefra-studio", "serverInfo.name is tefra-studio")
    check("tools" in result["capabilities"], "capabilities declare tools")

    status, _, text = http.post({"jsonrpc": "2.0", "method": "notifications/initialized"})
    check(status == 202 and text == "", "notifications/initialized answers 202 with no body")

    check(http.rpc("ping")["result"] == {}, "ping returns an empty result")

    tools = http.rpc("tools/list")["result"]["tools"]
    names = {t["name"] for t in tools}
    check(WORLD_TOOLS <= names, "tools/list has all world tools and echo")
    check(all(t["inputSchema"]["type"] == "object" and t["description"] for t in tools),
          "every tool has a description and an object inputSchema")


def world_checks(http, scratch):
    part_classes = http.ok("list_classes", {"class": "Part"})
    props = {p["name"]: p for p in part_classes["classes"][0]["properties"]}
    check(props["Size"]["type"] == "Vector3" and props["Shape"]["options"][0] == "Block",
          "list_classes describes Part with types and enum options")

    created = http.ok("create_instance", {
        "class": "Part", "parent_path": "Workspace", "name": "TestPart",
        "properties": {"Size": [2, 3, 4], "Anchored": True, "Shape": "Ball", "Material": "Metal"}})
    check(created["path"] == "Workspace.TestPart", "create_instance places the part under Workspace")
    part_id = created["id"]

    is_error, text = http.tool("set_properties", {
        "path": "Workspace.TestPart", "properties": {"Size": "big", "Nope": 1, "Shape": "Triangle"}})
    check(is_error and "Size" in text and "Nope" in text and "Shape" in text,
          "set_properties lists every invalid property at once")
    check(value_of(http.ok("get_instance", {"id": part_id}), "Size") == [2, 3, 4],
          "a failed set_properties changes nothing")

    http.ok("set_properties", {"id": part_id, "properties": {
        "Position": [1, 2, 3], "Color": [1, 0, 0], "Shape": "Wedge"}})
    inst = http.ok("get_instance", {"id": part_id})
    check(value_of(inst, "Position") == [1, 2, 3] and inst["properties"]["Position"]["type"] == "Vector3",
          "get_instance returns Position as a typed Vector3")
    check(value_of(inst, "Shape") == "Wedge" and value_of(inst, "Anchored") is True,
          "get_instance returns enums by name and bools as booleans")

    is_error, text = http.tool("get_instance", {"id": 999999})
    check(is_error and "no instance" in text, "an unknown id is a tool error the model can read")

    found = http.ok("find", {"name": "testpart"})
    check(any(r["id"] == part_id for r in found["results"]), "find matches names case-insensitively")
    found = http.ok("find", {"class": "BasePart", "under_path": "Workspace"})
    check(any(r["id"] == part_id for r in found["results"]), "find by class includes subclasses (BasePart)")

    tree = http.ok("get_tree", {"path": "Workspace", "max_depth": 1})
    check(any(c["id"] == part_id for c in tree["children"]), "get_tree lists the part under Workspace")

    clone = http.ok("clone", {"id": part_id, "name": "TestPartCopy"})
    check(clone["id"] != part_id and clone["path"] == "Workspace.TestPartCopy", "clone makes a named copy")
    moved = http.ok("reparent", {"id": clone["id"], "new_parent_path": "ServerScriptService"})
    check(moved["path"] == "ServerScriptService.TestPartCopy", "reparent moves the copy")

    script = http.ok("create_instance", {"class": "Script", "parent_path": "ServerScriptService", "name": "Main"})
    source = "using System;\nclass Main {}"
    written = http.ok("set_script", {"path": "ServerScriptService.Main", "source": source})
    check(written["lines"] == 2, "set_script reports the line count")
    check(http.ok("get_script", {"id": script["id"]})["source"] == source, "get_script returns the source back")
    is_error, _ = http.tool("set_script", {"id": part_id, "source": "x"})
    check(is_error, "set_script refuses a Part")

    deleted = http.ok("delete_instance", {"id": clone["id"]})
    check(deleted["deleted_id"] == clone["id"], "delete_instance deletes the copy")
    is_error, _ = http.tool("get_instance", {"id": clone["id"]})
    check(is_error, "a deleted instance is no longer found")
    is_error, _ = http.tool("delete_instance", {"path": "Workspace"})
    check(is_error, "services cannot be deleted")
    is_error, _ = http.tool("delete_instance", {"path": "game"})
    check(is_error, "the DataModel itself cannot be deleted")

    is_error, text = http.tool("create_instance", {"class": "Workspace", "parent_path": "game"})
    check(is_error and "service" in text, "create_instance refuses a second Workspace")
    is_error, text = http.tool("create_instance", {"class": 5, "parent_path": "Workspace"})
    check(is_error and "class" in text, "a wrong argument type is reported by name")
    is_error, text = http.tool("get_instance", {})
    check(is_error and "missing" in text, "a missing argument is reported")

    place = os.path.join(scratch, "level.tefra")
    saved = http.ok("save_place", {"path": place})
    check(saved["instances"] == 12 and os.path.exists(place), "save_place writes a .tefra file (8 services + 4 parts)")
    is_error, _ = http.tool("save_place", {"path": os.path.join(scratch, "level.json")})
    check(is_error, "save_place insists on the .tefra extension")
    http.ok("delete_instance", {"id": part_id})
    loaded = http.ok("load_place", {"path": place})
    check(loaded["instances"] == saved["instances"], "load_place restores every instance")
    again = http.ok("find", {"name": "TestPart", "exact": True})
    check(again["count"] == 1 and again["results"][0]["id"] == part_id,
          "load_place keeps the ids stored in the file")

    check(http.ok("echo", {"message": "hola"}) == "hola", "a tool added by the app (echo) works")

    reply = http.rpc("tools/call", {"name": "does_not_exist", "arguments": {}})
    check(reply["error"]["code"] == -32602, "an unknown tool is a JSON-RPC invalid-params error")

    status, _, text = http.post([{"jsonrpc": "2.0", "id": 9, "method": "ping"}])
    check(json.loads(text)["error"]["code"] == -32600, "JSON-RPC batches are refused (not in 2025-06-18)")


def bridge_checks(bridge, port):
    msgs = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize",
         "params": {"protocolVersion": PROTOCOL, "capabilities": {}, "clientInfo": {"name": "t", "version": "0"}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/call",
         "params": {"name": "get_instance", "arguments": {"path": "Workspace.Baseplate"}}},
        {"jsonrpc": "2.0", "id": 3, "method": "tools/call",
         "params": {"name": "echo", "arguments": {"message": "via stdio"}}},
    ]
    stdin_text = "\n".join(json.dumps(m) for m in msgs) + "\n"
    run = subprocess.run([bridge, "--url", "http://127.0.0.1:%d/mcp" % port], input=stdin_text,
                         capture_output=True, text=True, timeout=60)
    check(run.returncode == 0, "tefra_mcp exits cleanly when stdin closes")
    replies = [json.loads(line) for line in run.stdout.splitlines() if line.strip()]
    check([r["id"] for r in replies] == [1, 2, 3], "bridge answers each request once and skips notifications")
    check(replies[0]["result"]["protocolVersion"] == PROTOCOL, "bridge relays initialize")
    baseplate = json.loads(replies[1]["result"]["content"][0]["text"])
    check(value_of(baseplate, "Anchored") is True, "bridge relays tools/call results")
    check(replies[2]["result"]["content"][0]["text"] == "via stdio", "bridge keeps the session between calls")

    dead = subprocess.run([bridge, "--url", "http://127.0.0.1:1/mcp"], input=json.dumps(msgs[0]) + "\n",
                          capture_output=True, text=True, timeout=60)
    reply = json.loads(dead.stdout.strip())
    check(reply["id"] == 1 and reply["error"]["code"] == -32000,
          "bridge reports an unreachable server as a JSON-RPC error for that request")


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    demo, bridge = argv[1], argv[2]
    scratch = tempfile.mkdtemp(prefix="tefra_mcp_test_")
    # stderr goes to a file: nothing reads a pipe here, and a full pipe would block the server.
    with open(os.path.join(scratch, "demo.log"), "w") as log:
        proc = subprocess.Popen([demo, "--port", "0"], stdout=subprocess.PIPE, stderr=log, text=True)
        try:
            return run_checks(proc, bridge, scratch)
        finally:
            shutil.rmtree(scratch, ignore_errors=True)


def run_checks(proc, bridge, scratch):
    watchdog = threading.Timer(120, proc.kill)
    watchdog.start()
    try:
        announce = proc.stdout.readline().strip()
        if not announce.startswith("TEFRA_MCP_PORT="):
            raise Failure("demo did not announce its port: %r" % announce)
        port = int(announce.split("=", 1)[1])
        check(port > 0, "demo listens on a free loopback port (%d)" % port)

        transport_checks(port)
        http = Http(port)
        handshake_checks(http)
        world_checks(http, scratch)
        bridge_checks(bridge, port)
        print("all MCP checks passed")
        return 0
    finally:
        watchdog.cancel()
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except Failure as err:
        print("FAIL - " + str(err))
        sys.exit(1)
