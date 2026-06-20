// node baseline server for the comparison benchmark: raw http, /plaintext and /json, no framework.
// scale across cores with WORKERS=N (uses the built-in cluster module, the node equivalent of varn workers).
const http = require("http");
const cluster = require("cluster");

const port = parseInt(process.env.PORT || "3000", 10);
const workers = parseInt(process.env.WORKERS || "1", 10);

function serve() {
  http
    .createServer((req, res) => {
      if (req.url === "/plaintext") {
        res.writeHead(200, { "Content-Type": "text/plain" });
        res.end("Hello, World!");
      } else {
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ hello: "world" }));
      }
    })
    .listen(port, "127.0.0.1");
}

if (workers > 1 && cluster.isPrimary) {
  for (let i = 0; i < workers; i++) cluster.fork();
} else {
  serve();
}
