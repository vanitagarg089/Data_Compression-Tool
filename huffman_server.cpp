/*
 * ============================================================
 *  huffman_server.cpp  —  Huffman Compression HTTP Backend
 * ============================================================
 *  Build :  g++ -std=c++17 -O2 -o huffman_server huffman_server.cpp
 *  Run   :  ./huffman_server [port]   (default port 8080)
 *
 *  Endpoints
 *  ---------
 *  GET  /              → serves frontend/index.html
 *  POST /compress/text → raw text body        → JSON result
 *  POST /compress/file → multipart/form-data  → JSON result
 * ============================================================
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <queue>
#include <vector>
#include <algorithm>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

/* ============================================================
   1.  HUFFMAN CODING
   ============================================================ */

struct HuffNode {
    char      ch;
    int       freq;
    HuffNode* left;
    HuffNode* right;
    HuffNode(char c, int f) : ch(c), freq(f), left(nullptr), right(nullptr) {}
};

struct CmpNode {
    bool operator()(HuffNode* a, HuffNode* b) const { return a->freq > b->freq; }
};

/* Recursively assign binary codes */
void buildCodes(HuffNode* node,
                const std::string& prefix,
                std::map<char, std::string>& codes)
{
    if (!node) return;
    if (!node->left && !node->right) {          // leaf
        codes[node->ch] = prefix.empty() ? "0" : prefix;
        return;
    }
    buildCodes(node->left,  prefix + "0", codes);
    buildCodes(node->right, prefix + "1", codes);
}

void freeTree(HuffNode* node) {
    if (!node) return;
    freeTree(node->left);
    freeTree(node->right);
    delete node;
}

/* Result struct returned to the HTTP layer */
struct CompressResult {
    long long originalBits   = 0;
    long long compressedBits = 0;
    long long savedBits      = 0;
    int       savePct        = 0;
    int       uniqueChars    = 0;
    std::string topCodes;        // human-readable sample codes
};

CompressResult huffmanCompress(const std::string& text) {
    CompressResult res;
    if (text.empty()) return res;

    /* --- frequency table --- */
    std::map<char, int> freq;
    for (char c : text) freq[c]++;

    /* --- build min-heap --- */
    std::priority_queue<HuffNode*, std::vector<HuffNode*>, CmpNode> pq;
    for (auto& [c, f] : freq)
        pq.push(new HuffNode(c, f));

    /* edge case: single unique character */
    if (pq.size() == 1) {
        HuffNode* only = pq.top(); pq.pop();
        HuffNode* root = new HuffNode('\0', only->freq);
        root->left = only;
        pq.push(root);
    }

    /* --- merge nodes until one root remains --- */
    while (pq.size() > 1) {
        HuffNode* l = pq.top(); pq.pop();
        HuffNode* r = pq.top(); pq.pop();
        HuffNode* p = new HuffNode('\0', l->freq + r->freq);
        p->left  = l;
        p->right = r;
        pq.push(p);
    }

    HuffNode* root = pq.top(); pq.pop();

    /* --- assign codes --- */
    std::map<char, std::string> codes;
    buildCodes(root, "", codes);
    freeTree(root);

    /* --- compute bit counts --- */
    res.originalBits   = static_cast<long long>(text.size()) * 8;
    res.compressedBits = 0;
    for (char c : text)
        res.compressedBits += static_cast<long long>(codes[c].size());

    res.savedBits  = res.originalBits - res.compressedBits;
    res.savePct    = static_cast<int>(100.0 * res.savedBits / res.originalBits);
    res.uniqueChars = static_cast<int>(codes.size());

    /* --- top-5 most-frequent character codes --- */
    std::vector<std::pair<int,char>> sorted;
    for (auto& [c, f] : freq) sorted.push_back({f, c});
    std::sort(sorted.rbegin(), sorted.rend());

    std::ostringstream ss;
    int shown = 0;
    for (auto& [f, c] : sorted) {
        if (shown++ >= 5) break;
        std::string label;
        if      (c == '\n') label = "\\n";
        else if (c == '\t') label = "\\t";
        else if (c == ' ')  label = "SP";
        else                label = std::string(1, c);
        ss << label << "=" << codes[c] << " ";
    }
    res.topCodes = ss.str();
    return res;
}

/* ============================================================
   2.  HTTP HELPERS
   ============================================================ */

/* Serialize CompressResult → JSON string */
std::string toJSON(const CompressResult& r, const std::string& filename = "") {
    std::ostringstream js;
    js << "{"
       << "\"original\":"     << r.originalBits   << ","
       << "\"compressed\":"   << r.compressedBits  << ","
       << "\"saved\":"        << r.savedBits       << ","
       << "\"savePct\":"      << r.savePct         << ","
       << "\"uniqueChars\":"  << r.uniqueChars     << ","
       << "\"topCodes\":\""   << r.topCodes        << "\"";
    if (!filename.empty())
        js << ",\"filename\":\"" << filename << "\"";
    js << "}";
    return js.str();
}

/* Build a complete HTTP response string */
std::string httpResp(int code, const std::string& ctype, const std::string& body) {
    std::string status;
    switch (code) {
        case 200: status = "200 OK";              break;
        case 400: status = "400 Bad Request";     break;
        case 404: status = "404 Not Found";       break;
        default:  status = "500 Internal Error";  break;
    }
    std::ostringstream r;
    r << "HTTP/1.1 " << status << "\r\n"
      << "Content-Type: "   << ctype     << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n"
      << "Access-Control-Allow-Headers: Content-Type\r\n"
      << "Connection: close\r\n"
      << "\r\n"
      << body;
    return r.str();
}

/* Read a text file from disk into a string */
std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/* Extract multipart boundary token */
std::string getBoundary(const std::string& ct) {
    auto pos = ct.find("boundary=");
    if (pos == std::string::npos) return "";
    return "--" + ct.substr(pos + 9);
}

/* Parse a single-part multipart body; returns false on failure */
bool parseMultipart(const std::string& body,
                    const std::string& boundary,
                    std::string& filename,
                    std::string& filedata)
{
    auto bpos = body.find(boundary);
    if (bpos == std::string::npos) return false;

    auto cdpos = body.find("Content-Disposition:", bpos);
    if (cdpos == std::string::npos) return false;

    auto fnpos = body.find("filename=\"", cdpos);
    if (fnpos != std::string::npos) {
        fnpos += 10;
        auto fnend = body.find('"', fnpos);
        filename = body.substr(fnpos, fnend - fnpos);
    }

    /* skip past the part headers */
    auto hdrend = body.find("\r\n\r\n", cdpos);
    if (hdrend == std::string::npos) return false;
    hdrend += 4;

    /* find closing boundary */
    std::string endBound = boundary + "--";
    auto dataend = body.find(endBound, hdrend);
    if (dataend == std::string::npos) dataend = body.size();
    /* strip trailing CRLF before boundary */
    if (dataend >= 2 && body[dataend-2] == '\r' && body[dataend-1] == '\n')
        dataend -= 2;

    filedata = body.substr(hdrend, dataend - hdrend);
    return true;
}

/* ============================================================
   3.  REQUEST HANDLER
   ============================================================ */

void handleClient(int cfd) {
    /* ── read full request (up to 20 MB) ── */
    std::string raw;
    raw.reserve(8192);
    char buf[8192];
    int  n;

    while ((n = recv(cfd, buf, sizeof(buf), 0)) > 0) {
        raw.append(buf, n);
        auto hdrEnd = raw.find("\r\n\r\n");
        if (hdrEnd == std::string::npos) continue;

        /* check whether we have the full body */
        auto clpos = raw.find("Content-Length: ");
        if (clpos == std::string::npos) break;   // no body
        size_t clval = std::stoul(
            raw.substr(clpos + 16, raw.find("\r\n", clpos) - clpos - 16));
        if (raw.size() >= hdrEnd + 4 + clval) break;
    }

    if (raw.empty()) { close(cfd); return; }

    /* ── parse request line ── */
    std::istringstream iss(raw);
    std::string method, path, proto;
    iss >> method >> path >> proto;

    /* ── CORS preflight ── */
    if (method == "OPTIONS") {
        std::string resp = httpResp(200, "text/plain", "");
        send(cfd, resp.c_str(), resp.size(), 0);
        close(cfd); return;
    }

    /* ── body extraction ── */
    auto hdrEnd = raw.find("\r\n\r\n");
    std::string body = (hdrEnd != std::string::npos) ? raw.substr(hdrEnd + 4) : "";

    /* ================================================================
       GET /  →  serve frontend/index.html
    ================================================================ */
    if (method == "GET" && (path == "/" || path == "/index.html")) {
        std::string html = readFile("frontend/index.html");
        if (html.empty()) {
            // fallback message if frontend folder not present
            html = "<h1>Frontend not found</h1>"
                   "<p>Place <code>frontend/</code> folder next to the binary.</p>";
        }
        std::string resp = httpResp(200, "text/html; charset=utf-8", html);
        send(cfd, resp.c_str(), resp.size(), 0);
        close(cfd); return;
    }

    /* Serve other static frontend assets (CSS, JS) */
    if (method == "GET" && (path == "/style.css" || path == "/app.js")) {
        std::string asset = readFile("frontend" + path);
        std::string ctype = (path == "/style.css") ? "text/css" : "application/javascript";
        std::string resp  = httpResp(asset.empty() ? 404 : 200, ctype, asset);
        send(cfd, resp.c_str(), resp.size(), 0);
        close(cfd); return;
    }

    /* ================================================================
       POST /compress/text  →  raw text in body
    ================================================================ */
    if (method == "POST" && path == "/compress/text") {
        if (body.empty()) {
            auto resp = httpResp(400, "application/json", "{\"error\":\"empty body\"}");
            send(cfd, resp.c_str(), resp.size(), 0);
            close(cfd); return;
        }
        auto res  = huffmanCompress(body);
        auto json = toJSON(res);
        auto resp = httpResp(200, "application/json", json);
        send(cfd, resp.c_str(), resp.size(), 0);
        close(cfd); return;
    }

    /* ================================================================
       POST /compress/file  →  multipart/form-data
    ================================================================ */
    if (method == "POST" && path == "/compress/file") {
        /* extract Content-Type header */
        std::string ct;
        auto ctpos = raw.find("Content-Type: ");
        if (ctpos != std::string::npos) {
            auto cte = raw.find("\r\n", ctpos);
            ct = raw.substr(ctpos + 14, cte - ctpos - 14);
        }

        std::string boundary = getBoundary(ct);
        std::string filename, filedata;

        if (boundary.empty() || !parseMultipart(body, boundary, filename, filedata)) {
            /* fallback: treat raw body as file content */
            filedata = body;
            filename = "uploaded.bin";
        }

        if (filedata.empty()) {
            auto resp = httpResp(400, "application/json", "{\"error\":\"no file data\"}");
            send(cfd, resp.c_str(), resp.size(), 0);
            close(cfd); return;
        }

        auto res  = huffmanCompress(filedata);
        auto json = toJSON(res, filename);
        auto resp = httpResp(200, "application/json", json);
        send(cfd, resp.c_str(), resp.size(), 0);
        close(cfd); return;
    }

    /* ── 404 ── */
    auto resp = httpResp(404, "text/plain", "Not Found");
    send(cfd, resp.c_str(), resp.size(), 0);
    close(cfd);
}

/* ============================================================
   4.  MAIN — TCP accept loop
   ============================================================ */

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) port = std::atoi(argv[1]);

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port));

    if (bind(sfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    listen(sfd, 32);

    std::cout << "\n"
              << "╔══════════════════════════════════════════════╗\n"
              << "║   Huffman Compression Server  (C++ Backend)  ║\n"
              << "╚══════════════════════════════════════════════╝\n"
              << "  Port    : " << port << "\n"
              << "  Browser : http://localhost:" << port << "\n"
              << "  Press Ctrl+C to stop.\n\n";

    while (true) {
        sockaddr_in client{};
        socklen_t   len = sizeof(client);
        int cfd = accept(sfd, reinterpret_cast<sockaddr*>(&client), &len);
        if (cfd < 0) continue;
        handleClient(cfd);
    }
    return 0;
}
