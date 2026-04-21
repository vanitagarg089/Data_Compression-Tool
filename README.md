# Huffman Compression — Full Stack Project

A complete lossless text/file compression tool with a
pure C++ HTTP backend and a clean HTML/CSS/JS frontend.

## Project Structure

```
huffman_project/
├── frontend/
│   ├── index.html   ← UI markup
│   ├── style.css    ← All styles
│   └── app.js       ← Fetch calls & DOM logic
├── backend/
│   └── huffman_server.cpp  ← C++ server + Huffman algorithm
├── Makefile
└── README.md
```

## Quick Start

### 1 — Build the server

```bash
make
# or manually:
g++ -std=c++17 -O2 -o huffman_server backend/huffman_server.cpp
```

### 2 — Run the server

```bash
./huffman_server          # default port 8080
./huffman_server 9000     # custom port
```

### 3 — Open your browser

```
http://localhost:8080
```

---

## API Reference

| Method | Endpoint          | Body                    | Response    |
|--------|-------------------|-------------------------|-------------|
| GET    | `/`               | —                       | index.html  |
| GET    | `/style.css`      | —                       | style.css   |
| GET    | `/app.js`         | —                       | app.js      |
| POST   | `/compress/text`  | Raw text                | JSON        |
| POST   | `/compress/file`  | multipart/form-data     | JSON        |

### JSON Response

```json
{
  "original":    640,
  "compressed":  318,
  "saved":       322,
  "savePct":     50,
  "uniqueChars": 24,
  "topCodes":    "a=10 SP=01 e=001 t=000 s=1100",
  "filename":    "test.txt"
}
```

---

## How Huffman Coding Works

1. **Frequency table** — count occurrences of every character.
2. **Min-heap** — insert all characters sorted by frequency.
3. **Build tree** — repeatedly merge the two lowest-frequency nodes.
4. **Assign codes** — walk left → append `0`, walk right → append `1`.
5. **Encode** — replace each character with its variable-length code.

Frequent characters get short codes; rare ones get long codes.
Result: fewer total bits than a flat 8-bit ASCII encoding.

---

## Requirements

- Linux / macOS (POSIX sockets)
- `g++` with C++17 support (`g++ --version`)
- A modern web browser

## Clean build

```bash
make clean
```
